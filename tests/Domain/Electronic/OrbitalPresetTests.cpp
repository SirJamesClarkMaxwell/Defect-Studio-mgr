#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Domain/Electronic/HydrogenicOrbital.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Every two-centre preset is probed along this bond: A at the origin, B 1.5 A up the z
		// axis, so "along the bond" is +z and the two independent perpendiculars are x and y.
		constexpr float kBondLength = 1.5f;
		const glm::vec3 kCenterA = glm::vec3(0.0f);
		const glm::vec3 kCenterB = glm::vec3(0.0f, 0.0f, kBondLength);
		const glm::vec3 kMidpoint = glm::vec3(0.0f, 0.0f, 0.5f * kBondLength);

		[[nodiscard]] OrbitalPresetSettings BondSettings(int shell = 2, int lobeIndex = 0)
		{
			OrbitalPresetSettings settings;
			settings.centerA = kCenterA;
			settings.centerB = kCenterB;
			settings.shell = shell;
			settings.lobeIndex = lobeIndex;
			return settings;
		}

		// A point on the bond axis, `t` of the way from A to B.
		[[nodiscard]] glm::vec3 AlongBond(float t)
		{
			return kCenterA + (kCenterB - kCenterA) * t;
		}

		[[nodiscard]] float Sign(float value)
		{
			return value < 0.0f ? -1.0f : 1.0f;
		}

		// Peak amplitude of `wavefunction` on a coarse sphere of the given radius around `centre` -
		// enough to tell "this preset has amplitude in that region" from "it does not".
		[[nodiscard]] float PeakOnSphere(
			const OrbitalWavefunction &wavefunction, const glm::vec3 &centre, float radius)
		{
			const float pi = std::numbers::pi_v<float>;
			float peak = 0.0f;
			for (int thetaIndex = 0; thetaIndex <= 18; ++thetaIndex)
			{
				const float theta = pi * static_cast<float>(thetaIndex) / 18.0f;
				for (int phiIndex = 0; phiIndex < 36; ++phiIndex)
				{
					const float phi = 2.0f * pi * static_cast<float>(phiIndex) / 36.0f;
					const glm::vec3 offset = radius *
						glm::vec3(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi),
							std::cos(theta));
					peak = std::max(peak, std::abs(EvaluateOrbital(wavefunction, centre + offset)));
				}
			}
			return peak;
		}

		// Direction in which a single-centre preset has its largest amplitude, found on the same
		// coarse sphere. Pins the lobe conventions without hard-coding the amplitude.
		[[nodiscard]] glm::vec3 PeakDirection(const OrbitalWavefunction &wavefunction, float radius)
		{
			const float pi = std::numbers::pi_v<float>;
			glm::vec3 best(0.0f, 0.0f, 1.0f);
			float bestValue = -1.0f;
			for (int thetaIndex = 0; thetaIndex <= 60; ++thetaIndex)
			{
				const float theta = pi * static_cast<float>(thetaIndex) / 60.0f;
				for (int phiIndex = 0; phiIndex < 120; ++phiIndex)
				{
					const float phi = 2.0f * pi * static_cast<float>(phiIndex) / 120.0f;
					const glm::vec3 direction(
						std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
					const float value = EvaluateOrbital(wavefunction, direction * radius);
					if (value > bestValue)
					{
						bestValue = value;
						best = direction;
					}
				}
			}
			return best;
		}

		[[nodiscard]] float AngleBetweenDegrees(const glm::vec3 &a, const glm::vec3 &b)
		{
			const float cosine = glm::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f);
			return glm::degrees(std::acos(cosine));
		}
	} // namespace

	// --- single-centre presets ---------------------------------------------------------------

	TEST(OrbitalPresetTests, SIsASingleSphericalTerm)
	{
		OrbitalPresetSettings settings;
		settings.centerA = glm::vec3(1.0f, 2.0f, 3.0f);
		settings.shell = 2;
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::S, settings);

		ASSERT_EQ(wavefunction.terms.size(), 1u);
		EXPECT_EQ(wavefunction.terms[0].orbital.n, 2);
		EXPECT_EQ(wavefunction.terms[0].orbital.l, 0);
		EXPECT_EQ(wavefunction.terms[0].center, settings.centerA);

		const float radius = 2.0f;
		const float reference = EvaluateOrbital(wavefunction, settings.centerA + glm::vec3(radius, 0.0f, 0.0f));
		EXPECT_NEAR(
			EvaluateOrbital(wavefunction, settings.centerA + glm::vec3(0.0f, 0.0f, radius)), reference, 1e-6f);
	}

	TEST(OrbitalPresetTests, PLobeIndexSelectsTheAxis)
	{
		// lobeIndex 0 -> p_z, 1 -> p_x, 2 -> p_y (documented on OrbitalPresetSettings).
		const glm::vec3 expected[3] = {
			glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};
		for (int lobe = 0; lobe < 3; ++lobe)
		{
			OrbitalPresetSettings settings;
			settings.lobeIndex = lobe;
			const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::P, settings);
			EXPECT_LT(AngleBetweenDegrees(PeakDirection(wavefunction, 2.0f), expected[lobe]), 5.0f)
				<< "lobeIndex " << lobe;
		}
	}

	TEST(OrbitalPresetTests, DNeedsTheThirdShellAndIsClampedUpIntoIt)
	{
		OrbitalPresetSettings settings;
		settings.shell = 2; // no d in the second shell - must be clamped, not silently zeroed
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::D, settings);
		ASSERT_FALSE(wavefunction.terms.empty());
		EXPECT_GE(wavefunction.terms[0].orbital.n, 3);
		EXPECT_EQ(wavefunction.terms[0].orbital.l, 2);
		EXPECT_GT(PeakOnSphere(wavefunction, glm::vec3(0.0f), 4.0f), 0.0f);
	}

	TEST(OrbitalPresetTests, LobeIndexIsClampedRatherThanWrappingOrCrashing)
	{
		OrbitalPresetSettings settings;
		settings.lobeIndex = 99;
		EXPECT_FALSE(MakeOrbitalPreset(OrbitalPreset::Sp3, settings).terms.empty());
		settings.lobeIndex = -4;
		EXPECT_FALSE(MakeOrbitalPreset(OrbitalPreset::P, settings).terms.empty());
	}

	// --- hybrids -----------------------------------------------------------------------------

	TEST(OrbitalPresetTests, SpHybridPointsAlongPlusZAndItsPartnerAlongMinusZ)
	{
		const OrbitalWavefunction first = MakeOrbitalPreset(OrbitalPreset::Sp, OrbitalPresetSettings{});
		OrbitalPresetSettings second;
		second.lobeIndex = 1;
		const OrbitalWavefunction opposite = MakeOrbitalPreset(OrbitalPreset::Sp, second);

		// Two terms: the s and the p of the shell, on one centre.
		EXPECT_EQ(first.terms.size(), 2u);
		EXPECT_LT(AngleBetweenDegrees(PeakDirection(first, 2.0f), glm::vec3(0.0f, 0.0f, 1.0f)), 5.0f);
		EXPECT_LT(AngleBetweenDegrees(PeakDirection(opposite, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f)), 5.0f);

		// A hybrid is lopsided - that is what distinguishes it from the bare p it is built from.
		const float front = EvaluateOrbital(first, glm::vec3(0.0f, 0.0f, 2.0f));
		const float back = EvaluateOrbital(first, glm::vec3(0.0f, 0.0f, -2.0f));
		EXPECT_GT(front, 0.0f);
		EXPECT_GT(std::abs(front), std::abs(back));
	}

	TEST(OrbitalPresetTests, SpTwoHasThreeLobesAtOneHundredTwentyDegrees)
	{
		std::vector<glm::vec3> directions;
		for (int lobe = 0; lobe < 3; ++lobe)
		{
			OrbitalPresetSettings settings;
			settings.lobeIndex = lobe;
			directions.push_back(PeakDirection(MakeOrbitalPreset(OrbitalPreset::Sp2, settings), 2.0f));
		}
		EXPECT_LT(AngleBetweenDegrees(directions[0], glm::vec3(0.0f, 0.0f, 1.0f)), 5.0f);
		EXPECT_NEAR(AngleBetweenDegrees(directions[0], directions[1]), 120.0f, 6.0f);
		EXPECT_NEAR(AngleBetweenDegrees(directions[0], directions[2]), 120.0f, 6.0f);
		EXPECT_NEAR(AngleBetweenDegrees(directions[1], directions[2]), 120.0f, 6.0f);
		// All three share one plane, so their sum points nowhere.
		EXPECT_LT(glm::length(directions[0] + directions[1] + directions[2]), 0.15f);
	}

	TEST(OrbitalPresetTests, SpThreeHasFourLobesAtTheTetrahedralAngle)
	{
		std::vector<glm::vec3> directions;
		for (int lobe = 0; lobe < 4; ++lobe)
		{
			OrbitalPresetSettings settings;
			settings.lobeIndex = lobe;
			const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::Sp3, settings);
			// s plus all three p of the shell.
			EXPECT_EQ(wavefunction.terms.size(), 4u) << "lobeIndex " << lobe;
			directions.push_back(PeakDirection(wavefunction, 2.0f));
		}
		EXPECT_LT(AngleBetweenDegrees(directions[0], glm::vec3(0.0f, 0.0f, 1.0f)), 5.0f);
		for (std::size_t first = 0; first < directions.size(); ++first)
		{
			for (std::size_t second = first + 1; second < directions.size(); ++second)
			{
				EXPECT_NEAR(AngleBetweenDegrees(directions[first], directions[second]), 109.47f, 6.0f)
					<< "lobes " << first << " and " << second;
			}
		}
	}

	TEST(OrbitalPresetTests, OrientationTurnsASingleCentrePreset)
	{
		OrbitalPresetSettings settings;
		settings.orientation = glm::mat3(
			glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::Sp3, settings);
		// Lobe 0 left +z and landed on +x.
		EXPECT_LT(AngleBetweenDegrees(PeakDirection(wavefunction, 2.0f), glm::vec3(1.0f, 0.0f, 0.0f)), 5.0f);
	}

	// --- two-centre molecular orbitals --------------------------------------------------------

	TEST(OrbitalPresetTests, SigmaHasNoNodeBetweenTheNuclei)
	{
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::Sigma, BondSettings());
		ASSERT_EQ(wavefunction.terms.size(), 2u);
		EXPECT_EQ(wavefunction.terms[0].center, kCenterA);
		EXPECT_EQ(wavefunction.terms[1].center, kCenterB);

		const float middle = EvaluateOrbital(wavefunction, kMidpoint);
		EXPECT_GT(std::abs(middle), 0.0f);
		EXPECT_EQ(Sign(EvaluateOrbital(wavefunction, AlongBond(0.25f))), Sign(middle));
		EXPECT_EQ(Sign(EvaluateOrbital(wavefunction, AlongBond(0.75f))), Sign(middle));
	}

	TEST(OrbitalPresetTests, SigmaStarPutsItsNodeExactlyBetweenTheNuclei)
	{
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::SigmaStar, BondSettings());
		const float peak = PeakOnSphere(wavefunction, kMidpoint, 0.5f * kBondLength);
		ASSERT_GT(peak, 0.0f);
		EXPECT_LT(std::abs(EvaluateOrbital(wavefunction, kMidpoint)), 1e-4f * peak);
		EXPECT_NE(
			Sign(EvaluateOrbital(wavefunction, AlongBond(0.25f))),
			Sign(EvaluateOrbital(wavefunction, AlongBond(0.75f))));
	}

	TEST(OrbitalPresetTests, SigmaFallsBackToSOrbitalsInTheFirstShell)
	{
		// There is no 1p to build a sigma from - H2 is the s-s combination, not an empty orbital.
		const OrbitalWavefunction wavefunction =
			MakeOrbitalPreset(OrbitalPreset::Sigma, BondSettings(/*shell=*/1));
		ASSERT_EQ(wavefunction.terms.size(), 2u);
		EXPECT_EQ(wavefunction.terms[0].orbital.n, 1);
		EXPECT_EQ(wavefunction.terms[0].orbital.l, 0);
		EXPECT_GT(std::abs(EvaluateOrbital(wavefunction, kMidpoint)), 0.0f);
	}

	TEST(OrbitalPresetTests, PiVanishesAlongTheWholeBondAxis)
	{
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::Pi, BondSettings());
		const float peak = PeakOnSphere(wavefunction, kMidpoint, 0.5f * kBondLength);
		ASSERT_GT(peak, 0.0f);
		for (const float t : {-0.5f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f})
			EXPECT_LT(std::abs(EvaluateOrbital(wavefunction, AlongBond(t))), 1e-4f * peak) << "t = " << t;
	}

	TEST(OrbitalPresetTests, PiIsBondingAcrossTheMidpointAndPiStarIsNot)
	{
		const OrbitalWavefunction pi = MakeOrbitalPreset(OrbitalPreset::Pi, BondSettings());
		const OrbitalWavefunction piStar = MakeOrbitalPreset(OrbitalPreset::PiStar, BondSettings());

		// Find the perpendicular this degenerate pair chose, then walk along the bond at that offset.
		const glm::vec3 candidates[2] = {glm::vec3(0.6f, 0.0f, 0.0f), glm::vec3(0.0f, 0.6f, 0.0f)};
		const glm::vec3 offset =
			std::abs(EvaluateOrbital(pi, kMidpoint + candidates[0])) >
				std::abs(EvaluateOrbital(pi, kMidpoint + candidates[1]))
			? candidates[0]
			: candidates[1];

		const float piNear = EvaluateOrbital(pi, AlongBond(0.25f) + offset);
		const float piFar = EvaluateOrbital(pi, AlongBond(0.75f) + offset);
		ASSERT_GT(std::abs(piNear), 0.0f);
		EXPECT_EQ(Sign(piNear), Sign(piFar));
		EXPECT_GT(std::abs(EvaluateOrbital(pi, kMidpoint + offset)), 0.0f);

		const float starNear = EvaluateOrbital(piStar, AlongBond(0.25f) + offset);
		const float starFar = EvaluateOrbital(piStar, AlongBond(0.75f) + offset);
		ASSERT_GT(std::abs(starNear), 0.0f);
		EXPECT_NE(Sign(starNear), Sign(starFar));
		// The extra nodal plane through the midpoint is what makes pi* four lobes, not two.
		const float peak = PeakOnSphere(piStar, kMidpoint, 0.5f * kBondLength);
		EXPECT_LT(std::abs(EvaluateOrbital(piStar, kMidpoint + offset)), 1e-4f * peak);
	}

	TEST(OrbitalPresetTests, TheTwoPiOrientationsAreIndependent)
	{
		const OrbitalWavefunction firstPi = MakeOrbitalPreset(OrbitalPreset::Pi, BondSettings(2, 0));
		const OrbitalWavefunction secondPi = MakeOrbitalPreset(OrbitalPreset::Pi, BondSettings(2, 1));
		const glm::vec3 probeX = kMidpoint + glm::vec3(0.6f, 0.0f, 0.0f);
		const glm::vec3 probeY = kMidpoint + glm::vec3(0.0f, 0.6f, 0.0f);

		// Whichever perpendicular each one picked, one is blind exactly where the other peaks.
		const float firstX = std::abs(EvaluateOrbital(firstPi, probeX));
		const float firstY = std::abs(EvaluateOrbital(firstPi, probeY));
		const float secondX = std::abs(EvaluateOrbital(secondPi, probeX));
		const float secondY = std::abs(EvaluateOrbital(secondPi, probeY));
		EXPECT_GT(std::max(firstX, firstY), 0.0f);
		EXPECT_GT(std::max(secondX, secondY), 0.0f);
		EXPECT_NE(firstX > firstY, secondX > secondY);
	}

	TEST(OrbitalPresetTests, DeltaIsBuiltFromDOrbitalsWithTwoNodalPlanesOnTheAxis)
	{
		const OrbitalWavefunction wavefunction =
			MakeOrbitalPreset(OrbitalPreset::Delta, BondSettings(/*shell=*/3));
		ASSERT_EQ(wavefunction.terms.size(), 2u);
		EXPECT_EQ(wavefunction.terms[0].orbital.l, 2);

		const float peak = PeakOnSphere(wavefunction, kMidpoint, 0.5f * kBondLength);
		ASSERT_GT(peak, 0.0f);
		// The axis itself is a node, as for pi.
		EXPECT_LT(std::abs(EvaluateOrbital(wavefunction, kMidpoint)), 1e-4f * peak);

		// Four lobes around the axis: adjacent quadrants carry opposite phase, and the diagonals
		// between them are the two nodal planes that make this a delta rather than a pi.
		const float alongX = EvaluateOrbital(wavefunction, kMidpoint + glm::vec3(0.8f, 0.0f, 0.0f));
		const float alongY = EvaluateOrbital(wavefunction, kMidpoint + glm::vec3(0.0f, 0.8f, 0.0f));
		ASSERT_GT(std::abs(alongX), 0.0f);
		EXPECT_NE(Sign(alongX), Sign(alongY));
		const float diagonal = 0.8f / std::sqrt(2.0f);
		EXPECT_LT(
			std::abs(EvaluateOrbital(wavefunction, kMidpoint + glm::vec3(diagonal, diagonal, 0.0f))),
			1e-3f * std::abs(alongX));
	}

	TEST(OrbitalPresetTests, DeltaStarAddsTheMidpointNode)
	{
		const OrbitalWavefunction wavefunction =
			MakeOrbitalPreset(OrbitalPreset::DeltaStar, BondSettings(/*shell=*/3));
		const glm::vec3 offset(0.8f, 0.0f, 0.0f);
		const float nearSide = EvaluateOrbital(wavefunction, AlongBond(0.25f) + offset);
		const float farSide = EvaluateOrbital(wavefunction, AlongBond(0.75f) + offset);
		ASSERT_GT(std::abs(nearSide), 0.0f);
		EXPECT_NE(Sign(nearSide), Sign(farSide));
	}

	TEST(OrbitalPresetTests, HybridSigmaBondsPointTheirLobesAtEachOther)
	{
		// sp3-sigma is the ethane C-C picture: one hybrid lobe from each carbon, meeting head-on.
		// Four terms per centre, eight in all.
		const OrbitalWavefunction bonding =
			MakeOrbitalPreset(OrbitalPreset::Sp3Sigma, BondSettings(/*shell=*/2));
		EXPECT_EQ(bonding.terms.size(), 8u);

		const float middle = EvaluateOrbital(bonding, kMidpoint);
		EXPECT_GT(std::abs(middle), 0.0f);
		EXPECT_EQ(Sign(EvaluateOrbital(bonding, AlongBond(0.3f))), Sign(middle));
		EXPECT_EQ(Sign(EvaluateOrbital(bonding, AlongBond(0.7f))), Sign(middle));

		const OrbitalWavefunction antibonding =
			MakeOrbitalPreset(OrbitalPreset::Sp3SigmaStar, BondSettings(/*shell=*/2));
		const float peak = PeakOnSphere(antibonding, kMidpoint, 0.5f * kBondLength);
		ASSERT_GT(peak, 0.0f);
		EXPECT_LT(std::abs(EvaluateOrbital(antibonding, kMidpoint)), 1e-4f * peak);
	}

	TEST(OrbitalPresetTests, EverySpHybridSigmaPairBondsAndAntibonds)
	{
		const std::pair<OrbitalPreset, OrbitalPreset> pairs[3] = {
			{OrbitalPreset::SpSigma, OrbitalPreset::SpSigmaStar},
			{OrbitalPreset::Sp2Sigma, OrbitalPreset::Sp2SigmaStar},
			{OrbitalPreset::Sp3Sigma, OrbitalPreset::Sp3SigmaStar}};
		for (const auto &[bondingPreset, antibondingPreset] : pairs)
		{
			const OrbitalWavefunction bonding = MakeOrbitalPreset(bondingPreset, BondSettings());
			const OrbitalWavefunction antibonding = MakeOrbitalPreset(antibondingPreset, BondSettings());
			const float peak = PeakOnSphere(antibonding, kMidpoint, 0.5f * kBondLength);
			ASSERT_GT(peak, 0.0f) << OrbitalPresetName(antibondingPreset);
			EXPECT_GT(std::abs(EvaluateOrbital(bonding, kMidpoint)), 0.0f)
				<< OrbitalPresetName(bondingPreset);
			EXPECT_LT(std::abs(EvaluateOrbital(antibonding, kMidpoint)), 1e-4f * peak)
				<< OrbitalPresetName(antibondingPreset);
		}
	}

	TEST(OrbitalPresetTests, TwoCentrePresetsFollowAnArbitraryBondDirection)
	{
		// Same sigma*, bond turned onto an awkward diagonal - the node must travel with it.
		OrbitalPresetSettings settings;
		settings.centerA = glm::vec3(1.0f, -2.0f, 0.5f);
		settings.centerB = settings.centerA + glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)) * kBondLength;
		const glm::vec3 midpoint = 0.5f * (settings.centerA + settings.centerB);

		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::SigmaStar, settings);
		const float peak = PeakOnSphere(wavefunction, midpoint, 0.5f * kBondLength);
		ASSERT_GT(peak, 0.0f);
		EXPECT_LT(std::abs(EvaluateOrbital(wavefunction, midpoint)), 1e-4f * peak);

		const OrbitalWavefunction pi = MakeOrbitalPreset(OrbitalPreset::Pi, settings);
		const float piPeak = PeakOnSphere(pi, midpoint, 0.5f * kBondLength);
		ASSERT_GT(piPeak, 0.0f);
		for (const float t : {0.2f, 0.5f, 0.9f})
		{
			const glm::vec3 onAxis = settings.centerA + (settings.centerB - settings.centerA) * t;
			EXPECT_LT(std::abs(EvaluateOrbital(pi, onAxis)), 1e-4f * piPeak) << "t = " << t;
		}
	}

	TEST(OrbitalPresetTests, CoincidentCentresFallBackToAUnitSeparationInsteadOfDegenerating)
	{
		OrbitalPresetSettings settings;
		settings.centerA = glm::vec3(2.0f);
		settings.centerB = settings.centerA;
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::Sigma, settings);
		ASSERT_EQ(wavefunction.terms.size(), 2u);
		EXPECT_GT(glm::length(wavefunction.terms[1].center - wavefunction.terms[0].center), 0.0f);
	}

	// --- names -------------------------------------------------------------------------------

	TEST(OrbitalPresetNameTests, NamesRoundTrip)
	{
		constexpr OrbitalPreset kAll[] = {
			OrbitalPreset::S, OrbitalPreset::P, OrbitalPreset::D, OrbitalPreset::F,
			OrbitalPreset::Sp, OrbitalPreset::Sp2, OrbitalPreset::Sp3,
			OrbitalPreset::Sigma, OrbitalPreset::SigmaStar,
			OrbitalPreset::Pi, OrbitalPreset::PiStar,
			OrbitalPreset::Delta, OrbitalPreset::DeltaStar,
			OrbitalPreset::SpSigma, OrbitalPreset::SpSigmaStar,
			OrbitalPreset::Sp2Sigma, OrbitalPreset::Sp2SigmaStar,
			OrbitalPreset::Sp3Sigma, OrbitalPreset::Sp3SigmaStar};

		std::vector<std::string> seen;
		for (const OrbitalPreset preset : kAll)
		{
			const std::string name = OrbitalPresetName(preset);
			EXPECT_FALSE(name.empty());
			EXPECT_EQ(std::find(seen.begin(), seen.end(), name), seen.end()) << "duplicate name " << name;
			seen.push_back(name);

			OrbitalPreset parsed = OrbitalPreset::D;
			ASSERT_TRUE(ParseOrbitalPreset(name, parsed)) << name;
			EXPECT_EQ(parsed, preset) << name;
		}
	}

	TEST(OrbitalPresetNameTests, NamesAreTheStableOnesPersistenceWrites)
	{
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::S), "s");
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::Sp2), "sp2");
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::Sigma), "sigma");
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::PiStar), "pi*");
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::DeltaStar), "delta*");
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::Sp3SigmaStar), "sp3-sigma*");
	}

	TEST(OrbitalPresetNameTests, UnknownNameLeavesTheTargetUntouched)
	{
		OrbitalPreset preset = OrbitalPreset::Pi;
		EXPECT_FALSE(ParseOrbitalPreset("f-orbital-from-the-future", preset));
		EXPECT_EQ(preset, OrbitalPreset::Pi);
		EXPECT_FALSE(ParseOrbitalPreset("", preset));
		EXPECT_EQ(preset, OrbitalPreset::Pi);
	}

	// --- f orbitals -----------------------------------------------------------------------------

	namespace
	{
		// Fibonacci-sphere quadrature. The real harmonics are smooth, so an equal-area point set
		// integrates them to a few parts in ten thousand at this count - enough to tell an
		// orthonormal set from a mis-normalised one, which is the whole point of the check.
		[[nodiscard]] float SphereIntegral(int lA, int mA, int lB, int mB)
		{
			constexpr int kCount = 40000;
			const float golden = std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
			float sum = 0.0f;
			for (int i = 0; i < kCount; ++i)
			{
				const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(kCount);
				const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
				const float theta = golden * static_cast<float>(i);
				const glm::vec3 direction(radius * std::cos(theta), radius * std::sin(theta), z);
				sum += RealSphericalHarmonic(lA, mA, direction) * RealSphericalHarmonic(lB, mB, direction);
			}
			return 4.0f * std::numbers::pi_v<float> * sum / static_cast<float>(kCount);
		}
	} // namespace

	TEST(RealSphericalHarmonicTests, TheSevenFHarmonicsAreOrthonormal)
	{
		for (int m = -3; m <= 3; ++m)
			EXPECT_NEAR(SphereIntegral(3, m, 3, m), 1.0f, 0.02f) << "m = " << m;

		for (int mA = -3; mA <= 3; ++mA)
		{
			for (int mB = mA + 1; mB <= 3; ++mB)
				EXPECT_NEAR(SphereIntegral(3, mA, 3, mB), 0.0f, 0.02f) << mA << " vs " << mB;
			// And orthogonal to the lower shells, which is what stops an f from leaking into a
			// hybrid built beside it.
			EXPECT_NEAR(SphereIntegral(3, mA, 1, 0), 0.0f, 0.02f) << mA;
			EXPECT_NEAR(SphereIntegral(3, mA, 2, 0), 0.0f, 0.02f) << mA;
		}
	}

	TEST(RealSphericalHarmonicTests, FzCubedHasItsTwoConesAsWellAsItsTwoLobes)
	{
		// Y_30 goes as z(5z^2 - 3) on the unit sphere: positive up the axis, negative down it, and
		// - the part that makes an f orbital look like an f orbital - negative again in a cone
		// around +z inside the node at z = sqrt(3/5). A d_z2 has no such sign change.
		EXPECT_GT(RealSphericalHarmonic(3, 0, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
		EXPECT_LT(RealSphericalHarmonic(3, 0, glm::vec3(0.0f, 0.0f, -1.0f)), 0.0f);
		EXPECT_NEAR(RealSphericalHarmonic(3, 0, glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f, 1e-5f);

		const glm::vec3 insideTheNode = glm::normalize(glm::vec3(std::sqrt(3.0f), 0.0f, 1.0f)); // z = 0.5
		EXPECT_LT(RealSphericalHarmonic(3, 0, insideTheNode), 0.0f);
	}

	TEST(RealSphericalHarmonicTests, StopsAboveF)
	{
		EXPECT_FLOAT_EQ(RealSphericalHarmonic(4, 0, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
		EXPECT_FLOAT_EQ(RealSphericalHarmonic(3, 4, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
	}

	TEST(OrbitalPresetTests, TheFPresetBuildsAnFTermAndClampsTheShellUpToFour)
	{
		OrbitalPresetSettings settings;
		settings.shell = 2; // no 2f exists; the preset must lift it, not return nothing
		const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::F, settings);
		ASSERT_EQ(wavefunction.terms.size(), 1u);
		EXPECT_EQ(wavefunction.terms[0].orbital.l, 3);
		EXPECT_GE(wavefunction.terms[0].orbital.n, 4);
		EXPECT_GT(SuggestOrbitalExtent(wavefunction), 0.0f);
	}

	TEST(OrbitalPresetTests, TheSevenFLobesAreSevenDifferentOrbitals)
	{
		std::vector<int> seenM;
		for (int lobe = 0; lobe < 7; ++lobe)
		{
			OrbitalPresetSettings settings;
			settings.shell = 4;
			settings.lobeIndex = lobe;
			const OrbitalWavefunction wavefunction = MakeOrbitalPreset(OrbitalPreset::F, settings);
			ASSERT_EQ(wavefunction.terms.size(), 1u) << "lobe " << lobe;
			const int m = wavefunction.terms[0].orbital.m;
			EXPECT_LE(std::abs(m), 3) << "lobe " << lobe;
			EXPECT_EQ(std::find(seenM.begin(), seenM.end(), m), seenM.end()) << "lobe " << lobe;
			seenM.push_back(m);
		}
		// Out-of-range lobes clamp instead of producing an invalid orbital.
		OrbitalPresetSettings settings;
		settings.shell = 4;
		settings.lobeIndex = 99;
		const OrbitalWavefunction clamped = MakeOrbitalPreset(OrbitalPreset::F, settings);
		ASSERT_EQ(clamped.terms.size(), 1u);
		EXPECT_LE(std::abs(clamped.terms[0].orbital.m), 3);
	}

	TEST(OrbitalPresetNameTests, TheFPresetPersistsAsF)
	{
		EXPECT_STREQ(OrbitalPresetName(OrbitalPreset::F), "f");
		OrbitalPreset parsed = OrbitalPreset::S;
		ASSERT_TRUE(ParseOrbitalPreset("f", parsed));
		EXPECT_EQ(parsed, OrbitalPreset::F);
	}

	// --- grouping -------------------------------------------------------------------------------

	TEST(OrbitalPresetGroupTests, EveryPresetIsFiledInExactlyOneGroup)
	{
		constexpr OrbitalPreset kEveryPreset[] = {
			OrbitalPreset::S, OrbitalPreset::P, OrbitalPreset::D, OrbitalPreset::F,
			OrbitalPreset::Sp, OrbitalPreset::Sp2, OrbitalPreset::Sp3,
			OrbitalPreset::Sigma, OrbitalPreset::SigmaStar,
			OrbitalPreset::Pi, OrbitalPreset::PiStar,
			OrbitalPreset::Delta, OrbitalPreset::DeltaStar,
			OrbitalPreset::SpSigma, OrbitalPreset::SpSigmaStar,
			OrbitalPreset::Sp2Sigma, OrbitalPreset::Sp2SigmaStar,
			OrbitalPreset::Sp3Sigma, OrbitalPreset::Sp3SigmaStar};

		std::vector<OrbitalPreset> flattened;
		for (const OrbitalPresetGroup group : AllOrbitalPresetGroups())
		{
			const std::vector<OrbitalPreset> members = OrbitalPresetsInGroup(group);
			EXPECT_FALSE(members.empty()) << OrbitalPresetGroupName(group);
			for (const OrbitalPreset preset : members)
			{
				EXPECT_EQ(OrbitalPresetGroupOf(preset), group) << OrbitalPresetName(preset);
				flattened.push_back(preset);
			}
		}

		// The menu is generated from these groups, so "every preset appears exactly once" is the
		// property that keeps a newly added preset from silently never being drawable.
		ASSERT_EQ(flattened.size(), std::size(kEveryPreset));
		for (const OrbitalPreset preset : kEveryPreset)
		{
			EXPECT_EQ(std::count(flattened.begin(), flattened.end(), preset), 1)
				<< OrbitalPresetName(preset);
		}
	}

	TEST(OrbitalPresetGroupTests, GroupsComeInMenuOrderWithDistinctNames)
	{
		const std::vector<OrbitalPresetGroup> groups = AllOrbitalPresetGroups();
		ASSERT_EQ(groups.size(), 5u);
		EXPECT_EQ(groups.front(), OrbitalPresetGroup::Atomic);
		EXPECT_EQ(groups.back(), OrbitalPresetGroup::HybridBonding);

		std::vector<std::string> seen;
		for (const OrbitalPresetGroup group : groups)
		{
			const std::string name = OrbitalPresetGroupName(group);
			EXPECT_FALSE(name.empty());
			EXPECT_EQ(std::find(seen.begin(), seen.end(), name), seen.end()) << "duplicate " << name;
			seen.push_back(name);
		}
	}

	TEST(OrbitalPresetGroupTests, TheObviousMembershipsAreTheOnesTheUserExpects)
	{
		EXPECT_EQ(OrbitalPresetGroupOf(OrbitalPreset::F), OrbitalPresetGroup::Atomic);
		EXPECT_EQ(OrbitalPresetGroupOf(OrbitalPreset::Sp3), OrbitalPresetGroup::Hybrid);
		EXPECT_EQ(OrbitalPresetGroupOf(OrbitalPreset::Pi), OrbitalPresetGroup::Bonding);
		EXPECT_EQ(OrbitalPresetGroupOf(OrbitalPreset::DeltaStar), OrbitalPresetGroup::Antibonding);
		EXPECT_EQ(OrbitalPresetGroupOf(OrbitalPreset::Sp2SigmaStar), OrbitalPresetGroup::HybridBonding);
	}

	// --- what the lobe index actually selects -----------------------------------------------------

	TEST(OrbitalPresetMemberTests, TheCountMatchesHowManyDistinctOrbitalsThePresetCanMake)
	{
		// The number the UI offers has to be the number the preset really has, or someone dials a
		// lobe that silently clamps back onto one they already drew.
		const std::pair<OrbitalPreset, int> kExpected[] = {
			{OrbitalPreset::S, 1}, {OrbitalPreset::P, 3}, {OrbitalPreset::D, 5}, {OrbitalPreset::F, 7},
			{OrbitalPreset::Sp, 2}, {OrbitalPreset::Sp2, 3}, {OrbitalPreset::Sp3, 4},
			{OrbitalPreset::Sigma, 1}, {OrbitalPreset::SigmaStar, 1},
			{OrbitalPreset::Pi, 2}, {OrbitalPreset::PiStar, 2},
			{OrbitalPreset::Delta, 2}, {OrbitalPreset::DeltaStar, 2},
			{OrbitalPreset::Sp3Sigma, 1}};

		for (const auto &[preset, expected] : kExpected)
			EXPECT_EQ(OrbitalPresetMemberCount(preset), expected) << OrbitalPresetName(preset);
	}

	TEST(OrbitalPresetMemberTests, EveryMemberHasItsOwnNameAndOutOfRangeClamps)
	{
		constexpr OrbitalPreset kMultiMember[] = {OrbitalPreset::P, OrbitalPreset::D, OrbitalPreset::F,
			OrbitalPreset::Sp, OrbitalPreset::Sp2, OrbitalPreset::Sp3, OrbitalPreset::Pi,
			OrbitalPreset::Delta};

		for (const OrbitalPreset preset : kMultiMember)
		{
			const int count = OrbitalPresetMemberCount(preset);
			std::vector<std::string> seen;
			for (int lobe = 0; lobe < count; ++lobe)
			{
				const std::string name = OrbitalPresetMemberName(preset, lobe);
				EXPECT_FALSE(name.empty()) << OrbitalPresetName(preset) << " lobe " << lobe;
				EXPECT_EQ(std::find(seen.begin(), seen.end(), name), seen.end())
					<< "duplicate member name " << name;
				seen.push_back(name);
			}
			// A stale index left over from switching preset must still render something, not crash
			// or return null.
			EXPECT_STREQ(OrbitalPresetMemberName(preset, 999), OrbitalPresetMemberName(preset, count - 1));
			EXPECT_STREQ(OrbitalPresetMemberName(preset, -5), OrbitalPresetMemberName(preset, 0));
		}
	}

	// The display labels are a second table beside the identifiers, so the thing that can rot is
	// the pairing: a preset gaining a member without gaining a label would clamp two lobes onto one
	// menu row, and a caller that typesets the subscript run splits on '_'.
	TEST(OrbitalPresetMemberTests, EveryPresetAndMemberHasADisplayLabelBesideItsIdentifier)
	{
		for (const OrbitalPresetGroup group : AllOrbitalPresetGroups())
		{
			for (const OrbitalPreset preset : OrbitalPresetsInGroup(group))
			{
				EXPECT_STRNE(OrbitalPresetDisplayName(preset), "") << OrbitalPresetName(preset);
				const int count = OrbitalPresetMemberCount(preset);
				std::vector<std::string> seen;
				for (int lobe = 0; lobe < count; ++lobe)
				{
					const std::string label = OrbitalPresetMemberDisplayName(preset, lobe);
					EXPECT_FALSE(label.empty()) << OrbitalPresetName(preset) << " lobe " << lobe;
					EXPECT_EQ(std::find(seen.begin(), seen.end(), label), seen.end())
						<< "duplicate display label " << label;
					seen.push_back(label);
					// A '_' that opens a subscript run must have something in it.
					const std::size_t mark = label.find('_');
					if (mark != std::string::npos)
						EXPECT_NE(label.find_first_not_of(' ', mark + 1), std::string::npos) << label;
				}
				EXPECT_STREQ(
					OrbitalPresetMemberDisplayName(preset, 999), OrbitalPresetMemberDisplayName(preset, count - 1));
			}
		}

		// Greek where the identifier spells it out - the whole point of the second table.
		EXPECT_STREQ(OrbitalPresetDisplayName(OrbitalPreset::PiStar), "π*");
		EXPECT_STREQ(OrbitalPresetMemberDisplayName(OrbitalPreset::P, 2), "p_y");
	}

	TEST(OrbitalPresetMemberTests, TheNamesDescribeTheOrbitalThatIsActuallyBuilt)
	{
		// p_z must really be the m = 0 p orbital, not just be labelled that way. Same for d_z2.
		OrbitalPresetSettings settings;
		settings.shell = 3;
		settings.lobeIndex = 0;
		EXPECT_STREQ(OrbitalPresetMemberName(OrbitalPreset::P, 0), "p_z");
		const OrbitalWavefunction pz = MakeOrbitalPreset(OrbitalPreset::P, settings);
		ASSERT_EQ(pz.terms.size(), 1u);
		EXPECT_EQ(pz.terms[0].orbital.l, 1);
		EXPECT_EQ(pz.terms[0].orbital.m, 0);

		EXPECT_STREQ(OrbitalPresetMemberName(OrbitalPreset::D, 0), "d_z2");
		const OrbitalWavefunction dz2 = MakeOrbitalPreset(OrbitalPreset::D, settings);
		ASSERT_EQ(dz2.terms.size(), 1u);
		EXPECT_EQ(dz2.terms[0].orbital.l, 2);
		EXPECT_EQ(dz2.terms[0].orbital.m, 0);

		// And the two pi members really are the two different perpendiculars, not the same one
		// twice under two labels.
		OrbitalPresetSettings bond = BondSettings(/*shell=*/2, /*lobeIndex=*/0);
		const OrbitalWavefunction piX = MakeOrbitalPreset(OrbitalPreset::Pi, bond);
		bond.lobeIndex = 1;
		const OrbitalWavefunction piY = MakeOrbitalPreset(OrbitalPreset::Pi, bond);
		ASSERT_FALSE(piX.terms.empty());
		ASSERT_FALSE(piY.terms.empty());
		EXPECT_NE(piX.terms[0].orbital.m, piY.terms[0].orbital.m);
	}
} // namespace DefectStudio::Tests
