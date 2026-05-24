#include <gtest/gtest.h>

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

namespace DefectStudio::Tests
{
	TEST(PythonInterpreterTests, StartStopLifecycleOrReportsUnavailable)
	{
		PythonInterpreter interpreter;
		Result<void> startResult = interpreter.Start();
		if (!startResult)
		{
			EXPECT_EQ(startResult.Error().category, ErrorCategory::Python);
			EXPECT_FALSE(interpreter.IsRunning());
			return;
		}

		EXPECT_TRUE(interpreter.IsRunning());
		EXPECT_TRUE(PythonInterpreter::IsRuntimeInitialized());

		interpreter.Stop();
		EXPECT_FALSE(interpreter.IsRunning());
		EXPECT_FALSE(PythonInterpreter::IsRuntimeInitialized());
	}
} // namespace DefectStudio::Tests
