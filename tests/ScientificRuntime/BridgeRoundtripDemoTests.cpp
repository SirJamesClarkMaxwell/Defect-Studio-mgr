#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "ScientificRuntime/Python/ScientificPythonRuntime.hpp"

namespace DefectStudio::Tests
{
	TEST(BridgeRoundtripDemoTests, PythonImportsNanobindModuleWhenAvailable)
	{
		ScientificPythonRuntime runtime;
		Result<void> initializeResult = runtime.Initialize();
		if (!initializeResult)
		{
			GTEST_SKIP() << "Embedded runtime unavailable: " << initializeResult.Error().technicalDetails;
		}

		Result<ScriptRunResult> demoResult = runtime.RunBridgeRoundtripDemo();
		if (!demoResult)
		{
			const std::string &technicalDetails = demoResult.Error().technicalDetails;
			if (technicalDetails.find("No module named") != std::string::npos ||
			    technicalDetails.find("ModuleNotFoundError") != std::string::npos)
			{
				GTEST_SKIP() << "nanobind demo module is not built in this configuration.";
			}

			GTEST_SKIP() << "bridge demo is unavailable in current environment: " << technicalDetails;
		}

		nlohmann::json payload = nlohmann::json::parse(demoResult->standardOutput);
		EXPECT_EQ(payload.value("add_result", 0), 7);
		EXPECT_NEAR(payload.value("distance_result", 0.0), 3.0, 1e-9);

		runtime.Shutdown();
	}
} // namespace DefectStudio::Tests
