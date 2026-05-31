#include <gtest/gtest.h>

#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio::Tests
{
	TEST(ScriptBridgeUtilsTests, ReturnsFirstValidJsonLine)
	{
		const std::string rawOutput =
			"ASE bridge started\n"
			"{\"status\":\"ok\",\"step\":1}\n"
			"{\"status\":\"secondary\",\"step\":2}\n";

		const std::string jsonLine = ExtractJsonLineFromOutput(rawOutput);
		EXPECT_EQ(jsonLine, "{\"status\":\"ok\",\"step\":1}");
	}

	TEST(ScriptBridgeUtilsTests, ReturnsEmptyWhenNoValidJsonLineExists)
	{
		const std::string rawOutput =
			"line one\n"
			"line two\n";

		EXPECT_TRUE(ExtractJsonLineFromOutput(rawOutput).empty());
	}
} // namespace DefectStudio::Tests
