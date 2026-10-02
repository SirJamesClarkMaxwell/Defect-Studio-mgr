#include <gtest/gtest.h>

#include <cmath>
#include <optional>

#include "Domain/Electronic/HydrogenicOrbital.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Carbon-like 2p/sp3 size, so 1 A is well outside the 2s radial node and inside the lobe.
		constexpr float kCarbonCharge = 3.25f;
		constexpr float kProbeRadius = 1.0f;

		[[nodiscard]] float ValueAlong(OrbitalPreset preset, int lobeIndex, int shell, const glm::vec3 &direction)
		{
			OrbitalPresetSettings settings;
			settings.lobeIndex = lobeIndex;
			settings.shell = shell;
			settings.effectiveCharge = kCarbonCharge;
			return EvaluateOrbital(MakeOrbitalPreset(preset, settings), kProbeRadius * glm::normalize(direction));
		}

		void ExpectAxis(OrbitalPreset preset, int lobeIndex, const glm::vec3 &expected)
		{
			const std::optional<glm::vec3> axis = OrbitalPresetMemberAxis(preset, lobeIndex);
			ASSERT_TRUE(axis.has_value()) << OrbitalPresetName(preset) << " #" << lobeIndex;
			EXPECT_NEAR(glm::length(*axis - glm::normalize(expected)), 0.0f, 1e-5f)
				<< OrbitalPresetName(preset) << " #" << lobeIndex;
		}
	} // namespace

	TEST(OrbitalPresetMemberAxisTests, PMembersPointAlongTheirAxes)
	{
		ExpectAxis(OrbitalPreset::P, 0, glm::vec3(0, 0, 1));
		ExpectAxis(OrbitalPreset::P, 1, glm::vec3(1, 0, 0));
		ExpectAxis(OrbitalPreset::P, 2, glm::vec3(0, 1, 0));
	}

	TEST(OrbitalPresetMemberAxisTests, DMembersWithALobeAxis)
	{
		ExpectAxis(OrbitalPreset::D, 0, glm::vec3(0, 0, 1));
		ExpectAxis(OrbitalPreset::D, 1, glm::vec3(1, 0, 1));
		ExpectAxis(OrbitalPreset::D, 2, glm::vec3(0, 1, 1));
		ExpectAxis(OrbitalPreset::D, 3, glm::vec3(1, 0, 0));
		ExpectAxis(OrbitalPreset::D, 4, glm::vec3(1, 1, 0));
	}

	TEST(OrbitalPresetMemberAxisTests, NoAxisForSphericalTwoCentreOrMostF)
	{
		EXPECT_FALSE(OrbitalPresetMemberAxis(OrbitalPreset::S, 0).has_value());
		EXPECT_FALSE(OrbitalPresetMemberAxis(OrbitalPreset::Sigma, 0).has_value());
		EXPECT_FALSE(OrbitalPresetMemberAxis(OrbitalPreset::Sp3Sigma, 0).has_value());
		EXPECT_FALSE(OrbitalPresetMemberAxis(OrbitalPreset::PiStar, 1).has_value());
		EXPECT_FALSE(OrbitalPresetMemberAxis(OrbitalPreset::F, 3).has_value());
		ExpectAxis(OrbitalPreset::F, 0, glm::vec3(0, 0, 1));
	}

	TEST(OrbitalPresetMemberAxisTests, AxisIsClampedLikeLobeIndex)
	{
		ExpectAxis(OrbitalPreset::P, 99, glm::vec3(0, 1, 0));
		ExpectAxis(OrbitalPreset::P, -4, glm::vec3(0, 0, 1));
	}

	// The axis is the member's MAIN lobe: the wavefunction is positive along it and larger there than
	// along the opposite direction or along any other member's axis. That is what "aim this lobe"
	// has to mean for a hybrid, whose small back lobe has the opposite sign.
	TEST(OrbitalPresetMemberAxisTests, HybridAxesAreTheirPositiveMainLobes)
	{
		for (const auto &[preset, count] :
			 {std::pair{OrbitalPreset::Sp, 2}, std::pair{OrbitalPreset::Sp2, 3}, std::pair{OrbitalPreset::Sp3, 4}})
		{
			for (int lobe = 0; lobe < count; ++lobe)
			{
				const std::optional<glm::vec3> axis = OrbitalPresetMemberAxis(preset, lobe);
				ASSERT_TRUE(axis.has_value());
				EXPECT_NEAR(glm::length(*axis), 1.0f, 1e-5f);
				const float along = ValueAlong(preset, lobe, 2, *axis);
				EXPECT_GT(along, 0.0f) << OrbitalPresetName(preset) << " #" << lobe;
				EXPECT_GT(along, std::abs(ValueAlong(preset, lobe, 2, -*axis)));
				for (int other = 0; other < count; ++other)
				{
					if (other == lobe)
						continue;
					EXPECT_GT(along, ValueAlong(preset, lobe, 2, *OrbitalPresetMemberAxis(preset, other)))
						<< OrbitalPresetName(preset) << " #" << lobe << " vs #" << other;
				}
			}
		}
	}

	TEST(OrbitalPresetMemberAxisTests, AtomicAxesArePositive)
	{
		for (int lobe = 0; lobe < 3; ++lobe)
			EXPECT_GT(ValueAlong(OrbitalPreset::P, lobe, 2, *OrbitalPresetMemberAxis(OrbitalPreset::P, lobe)), 0.0f);
		for (int lobe = 0; lobe < 5; ++lobe)
			EXPECT_GT(ValueAlong(OrbitalPreset::D, lobe, 3, *OrbitalPresetMemberAxis(OrbitalPreset::D, lobe)), 0.0f)
				<< "d #" << lobe;
	}
} // namespace DefectStudio::Tests
