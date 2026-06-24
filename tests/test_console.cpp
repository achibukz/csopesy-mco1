#include <gtest/gtest.h>

#include "cli/Console.h"

// GP/CM CLI: REPL must ignore empty and whitespace-only input without throwing.
TEST(ConsoleTest, EmptyAndWhitespaceInputIsIgnored) {
    Console c;
    EXPECT_NO_THROW(c.dispatch(""));
    EXPECT_NO_THROW(c.dispatch("    "));
    EXPECT_NO_THROW(c.dispatch("\t\t  \t"));
}

// GP/CM CLI: excess internal whitespace must still tokenize cleanly. 'exit' is
// the only command allowed pre-initialize per CM ("No command other than
// `exit` is valid before `initialize`"), so it is safe to test here.
TEST(ConsoleTest, ExcessWhitespaceTokenizesCleanly) {
    Console c;
    EXPECT_NO_THROW(c.dispatch("    exit   "));
}

// CM: only initialize and exit are valid before initialize.
TEST(ConsoleTest, NonInitializeCommandsBeforeInitAreRejectedQuietly) {
    Console c;
    EXPECT_NO_THROW(c.dispatch("clear"));
    EXPECT_NO_THROW(c.dispatch("scheduler-start"));
    EXPECT_NO_THROW(c.dispatch("screen -ls"));
    EXPECT_NO_THROW(c.dispatch("report-util"));
}
