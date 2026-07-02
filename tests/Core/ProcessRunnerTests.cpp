#include <gtest/gtest.h>

#include <chrono>

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Core/Platform/ProcessRunner.hpp"

namespace DefectStudio::Tests
{
	TEST(ProcessRunnerTests, CapturesStdoutAndStderr)
	{
		Platform::ProcessRunOptions options;
		options.executable = Platform::ResolvePreferredPythonExecutable();
		options.arguments = {
			"-c",
			"import sys; print('process-out'); print('process-err', file=sys.stderr)"};
		options.workingDirectory = FileSystem::CurrentPath();

		Result<Platform::ProcessRunResult> result = Platform::RunProcess(options);
		if (!result.HasValue())
			GTEST_SKIP() << result.Error().technicalDetails;

		EXPECT_EQ(result->exitCode, 0);
		EXPECT_NE(result->standardOutput.find("process-out"), std::string::npos);
		EXPECT_NE(result->standardError.find("process-err"), std::string::npos);
	}

	TEST(ProcessRunnerTests, CapturesNonZeroExitCode)
	{
		Platform::ProcessRunOptions options;
		options.executable = Platform::ResolvePreferredPythonExecutable();
		options.arguments = {"-c", "import sys; sys.exit(7)"};
		options.workingDirectory = FileSystem::CurrentPath();

		Result<Platform::ProcessRunResult> result = Platform::RunProcess(options);
		if (!result.HasValue())
			GTEST_SKIP() << result.Error().technicalDetails;

		EXPECT_EQ(result->exitCode, 7);
	}

	TEST(ProcessRunnerTests, TerminatesOnTimeout)
	{
		Platform::ProcessRunOptions options;
		options.executable = Platform::ResolvePreferredPythonExecutable();
		options.arguments = {"-c", "import time; time.sleep(5)"};
		options.workingDirectory = FileSystem::CurrentPath();
		options.timeout = std::chrono::milliseconds(100);

		Result<Platform::ProcessRunResult> result = Platform::RunProcess(options);
		if (!result.HasValue())
			GTEST_SKIP() << result.Error().technicalDetails;

		EXPECT_TRUE(result->timedOut);
		EXPECT_TRUE(result->terminated);
		EXPECT_EQ(result->exitCode, -1);
	}
} // namespace DefectStudio::Tests
