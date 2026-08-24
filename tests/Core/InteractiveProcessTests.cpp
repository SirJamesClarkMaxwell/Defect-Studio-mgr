#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "Core/Platform/InteractiveProcess.hpp"
#include "Core/Platform/PlatformPythonRuntime.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Polls until the needle shows up or the deadline passes - output arrives on a
		// background thread, so a single PollOutput() right after WriteLine() would race it.
		std::string CollectOutputUntil(Platform::InteractiveProcess &process, const std::string &needle)
		{
			std::string collected;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (collected.find(needle) == std::string::npos && std::chrono::steady_clock::now() < deadline)
			{
				collected += process.PollOutput();
				if (collected.find(needle) == std::string::npos)
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			return collected;
		}
	} // namespace

	TEST(InteractiveProcessTests, EchoesWrittenLineBack)
	{
		Platform::InteractiveProcessOptions options;
		options.executable = Platform::ResolvePreferredPythonExecutable();
		options.arguments = {"-u", "-c", "import sys\nfor line in sys.stdin:\n    print('echo:' + line.strip(), flush=True)\n"};
		options.workingDirectory = FileSystem::CurrentPath();

		Platform::InteractiveProcess process;
		VoidResult started = process.Start(options);
		if (!started.HasValue())
			GTEST_SKIP() << started.Error().technicalDetails;

		EXPECT_TRUE(process.IsRunning());
		process.WriteLine("hello");

		const std::string collected = CollectOutputUntil(process, "echo:hello");
		EXPECT_NE(collected.find("echo:hello"), std::string::npos);

		process.Terminate();
		EXPECT_FALSE(process.IsRunning());
	}

	TEST(InteractiveProcessTests, TerminateStopsALongRunningProcess)
	{
		Platform::InteractiveProcessOptions options;
		options.executable = Platform::ResolvePreferredPythonExecutable();
		options.arguments = {"-c", "import time; time.sleep(30)"};
		options.workingDirectory = FileSystem::CurrentPath();

		Platform::InteractiveProcess process;
		VoidResult started = process.Start(options);
		if (!started.HasValue())
			GTEST_SKIP() << started.Error().technicalDetails;

		EXPECT_TRUE(process.IsRunning());
		process.Terminate();
		EXPECT_FALSE(process.IsRunning());
	}
} // namespace DefectStudio::Tests
