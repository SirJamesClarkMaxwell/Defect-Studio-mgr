#include "Core/Types/Error.hpp"
#include <gtest/gtest.h>

using namespace DefectStudio;

TEST(StructuredError, IsRecoverableTrueForNonFatal)
{
    StructuredError e;
    e.severity = Severity::Error;
    EXPECT_TRUE(e.IsRecoverable());
}

TEST(StructuredError, IsRecoverableFalseForFatal)
{
    StructuredError e;
    e.severity = Severity::Fatal;
    EXPECT_FALSE(e.IsRecoverable());
}

TEST(Result, StoresValueAndError)
{
    Result<int> success = 42;
    EXPECT_TRUE(success);
    EXPECT_EQ(success.Value(), 42);

    Result<int> failure = StructuredError{ErrorCategory::Validation, Severity::Error, "bad input", "details", "fix it"};
    EXPECT_FALSE(failure);
    EXPECT_EQ(failure.Error().userMessage, "bad input");
}
