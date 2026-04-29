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
