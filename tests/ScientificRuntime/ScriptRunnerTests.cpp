#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "ScientificRuntime/Python/ScriptRunner.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	TEST(ScriptRunnerTests, RunsSmokeScriptAndReturnsJsonPayload)
	{
		ScriptRunner runner;
		ScriptRunOptions options;
		options.scriptPath = PythonExamplePath("script_runner_smoke.py");
		options.arguments = { "alpha", "beta" };
		options.workingDirectory = FindRepoRoot();

		Result<ScriptRunResult> runResult = runner.RunFile(options);
		if (!runResult)
		{
			GTEST_SKIP() << "Python smoke script unavailable in local environment: "
			             << runResult.Error().technicalDetails;
		}

		ASSERT_EQ(runResult->exitCode, 0);
		ASSERT_FALSE(runResult->standardOutput.empty());

		nlohmann::json payload = nlohmann::json::parse(runResult->standardOutput);
		EXPECT_EQ(payload.value("argv_count", 0), 2);
		EXPECT_EQ(payload.at("argv").at(0).get<std::string>(), "alpha");
		EXPECT_EQ(payload.at("argv").at(1).get<std::string>(), "beta");
	}
} // namespace DefectStudio::Tests
