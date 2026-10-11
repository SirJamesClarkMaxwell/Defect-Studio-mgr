#include <gtest/gtest.h>

#include <cmath>
#include <nlohmann/json.hpp>

#include "ScientificRuntime/Python/VaspOutputBridge.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		nlohmann::json Payload()
		{
			return {{"path", "."}, {"gap", nullptr}, {"orbitals", nullptr}, {"orbitals_error", nullptr},
				{"summary", {{"energy_trend", {-10.0, -11.0}}, {"force_trend", {0.3, nullptr}},
					{"finished", false}, {"reached_accuracy", true}, {"nsw", 40}, {"ibrion", 2}, {"ediffg", -0.02},
					{"ks_band_start", 2}, {"ks_band_end", 3}, {"ks_steps", nlohmann::json::array()}}}};
		}

		nlohmann::json KsStep()
		{
			nlohmann::json step = {{"step", 2}, {"fermi", 0.5}, {"blocks", nlohmann::json::array()}};
			for (const int spin : {1, 2})
				for (const int kpoint : {1, 2})
					step["blocks"].push_back({{"spin", spin}, {"kpoint", kpoint}, {"bands", {2, 3}},
						{"energies", {-1.0, 1.0}}, {"occupations", {0.5, 0.0}}});
			return step;
		}
	}

	TEST(VaspOutputBridgeTests, ReadsWindowedKsSnapshotsAndMissingForces)
	{
		auto payload = Payload();
		payload["summary"]["ks_steps"].push_back(KsStep());
		const auto parsed = ParseVaspOutputJson(payload.dump());
		ASSERT_TRUE(parsed);
		const auto &summary = parsed->summary;
		ASSERT_EQ(summary.forceTrend.size(), 2u);
		EXPECT_DOUBLE_EQ(summary.forceTrend[0], 0.3);
		EXPECT_TRUE(std::isnan(summary.forceTrend[1]));
		EXPECT_EQ(summary.finished, false);
		EXPECT_EQ(summary.reachedAccuracy, true);
		ASSERT_EQ(summary.ksSteps.size(), 1u);
		EXPECT_EQ(summary.ksSteps[0].step, 2);
		EXPECT_EQ(summary.ksSteps[0].fermi, 0.5);
		ASSERT_EQ(summary.ksSteps[0].blocks.size(), 4u);
		EXPECT_EQ(summary.ksSteps[0].blocks.back().spin, 2);
		EXPECT_EQ(summary.ksSteps[0].blocks.back().kpoint, 2);
		EXPECT_EQ(summary.ksSteps[0].blocks.back().bands, (std::vector<int>{2, 3}));
	}

	TEST(VaspOutputBridgeTests, OldSummaryCacheDoesNotRequireNewFields)
	{
		auto payload = Payload();
		payload["summary"] = {{"energy_trend", {-10.0, -11.0}}, {"final_energy", -11.0}};
		const auto parsed = ParseVaspOutputJson(payload.dump());
		ASSERT_TRUE(parsed);
		EXPECT_TRUE(parsed->summary.ksSteps.empty());
		EXPECT_FALSE(parsed->summary.finished.has_value());
		EXPECT_EQ(parsed->summary.finalEnergy, -11.0);
	}

	TEST(VaspOutputBridgeTests, RejectsMismatchedKsArraysAndStepsOutsideTrajectory)
	{
		auto payload = Payload();
		auto step = KsStep();
		step["blocks"][0]["occupations"] = {0.5};
		payload["summary"]["ks_steps"] = nlohmann::json::array({step});
		EXPECT_FALSE(ParseVaspOutputJson(payload.dump()));
		step = KsStep();
		step["step"] = 3;
		payload["summary"]["ks_steps"] = nlohmann::json::array({step});
		EXPECT_FALSE(ParseVaspOutputJson(payload.dump()));
	}
} // namespace DefectStudio::Tests
