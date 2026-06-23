#include "process/Process.h"

#include <climits>
#include <utility>

namespace {

// Fully-expanded count of atomic instructions a program will execute, accounting
// for FOR repeats recursively. Clamped to INT_MAX so it fits getTotalLines()'s
// int return without overflow (depth is bounded to 3 by the generator).
long long countAtomics(const std::vector<std::unique_ptr<IInstruction>>& list) {
    long long sum = 0;
    for (const auto& ins : list) {
        if (ins->isBlock()) {
            long long inner = countAtomics(ins->body());
            sum += static_cast<long long>(ins->repeats()) * inner;
        } else {
            sum += 1;
        }
        if (sum > INT_MAX) return INT_MAX;
    }
    return sum;
}

}  // namespace

Process::Process(int pid, std::string name,
                 std::vector<std::unique_ptr<IInstruction>> program)
    : pid_(pid),
      name_(std::move(name)),
      program_(std::move(program)),
      totalInstructions_(static_cast<int>(countAtomics(program_))),
      createdAt_(std::chrono::system_clock::now()) {
    if (totalInstructions_ == 0) {
        // Nothing to run — already done (handles zero-length processes).
        state_.store(ProcessState::FINISHED);
    } else {
        // Seed the execution stack with a single pass over the top-level program.
        execStack_.push_back(Frame{&program_, 0, 1});
    }
}

const IInstruction* Process::nextAtomic() {
    while (!execStack_.empty()) {
        Frame& f = execStack_.back();

        if (f.ip >= f.list->size()) {
            // One pass of this frame complete.
            if (--f.loopsRemaining > 0) {
                f.ip = 0;            // repeat the loop body
                continue;
            }
            execStack_.pop_back();   // block done
            continue;
        }

        const IInstruction* instr = (*f.list)[f.ip].get();
        ++f.ip;  // advance before descending/returning (f may dangle after push_back)

        if (instr->isBlock()) {
            if (instr->repeats() > 0 && !instr->body().empty()) {
                execStack_.push_back(Frame{&instr->body(), 0, instr->repeats()});
            }
            continue;  // descend into the loop; no atomic consumed yet
        }
        return instr;
    }
    return nullptr;
}

void Process::executeNext(uint64_t /*currentTick*/) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_.load() == ProcessState::FINISHED) return;

    const IInstruction* instr = nextAtomic();
    if (!instr) {
        state_.store(ProcessState::FINISHED);
        return;
    }

    state_.store(ProcessState::RUNNING);
    instr->execute(*this);  // may call requestSleep() -> state becomes SLEEPING
    currentLine_.fetch_add(1);

    // Finish only once every atomic instruction has run and we are not parked
    // in a SLEEP. (A trailing SLEEP transitions to FINISHED on the next wake.)
    if (state_.load() != ProcessState::SLEEPING &&
        currentLine_.load() >= totalInstructions_) {
        state_.store(ProcessState::FINISHED);
    }
}

void Process::tickSleep() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_.load() != ProcessState::SLEEPING) return;
    if (sleepRemaining_ > 0) --sleepRemaining_;
    if (sleepRemaining_ == 0) state_.store(ProcessState::READY);
}

std::vector<std::string> Process::getPrintLog() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return printLog_;  // copy
}

// ---- Mutators (mutex_ already held by executeNext) ----

uint16_t Process::getVar(const std::string& name) {
    auto [it, inserted] = variables_.try_emplace(name, uint16_t{0});  // auto-declare as 0
    (void)inserted;
    return it->second;
}

void Process::setVar(const std::string& name, uint16_t value) {
    variables_[name] = value;
}

void Process::appendPrint(const std::string& msg) {
    printLog_.push_back(msg);
}

void Process::requestSleep(uint8_t ticks) {
    if (ticks == 0) return;  // no-op sleep keeps the CPU
    sleepRemaining_ = ticks;
    state_.store(ProcessState::SLEEPING);
}
