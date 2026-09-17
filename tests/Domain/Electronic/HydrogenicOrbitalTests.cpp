#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Domain/Electronic/HydrogenicOrbital.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr float kA0 = kBohrRadiusAngstrom;

		// Integral of R_nl^2 r^2 dr over [0, cutoff) by the midpoint rule. Fine enough (dr = 0.002 A
		// out to 60 A) that a correct radial function lands within 1e-3 of 1 - checking the actual
		// normalisation constant rather than only the shape.
		[[nodiscard]] float RadialNorm(int n, int l, float effectiveCharge)
		{
			constexpr float kStep = 0.002f;
			constexpr float kCutoff = 60.0f;
			float total = 0.0f;
			for (float radius = 0.5f * kStep; radius < kCutoff; radius += kStep)
			{
				const float value = HydrogenicRadial(n, l, effectiveCharge, radius);
				total += value * value * radius * radius * kStep;
			}
			return total;
		}

		// Integral of Y_a * Y_b over the unit sphere, midpoint rule in (theta, phi) with the sin
		// weight. Used for both normalisation (a == b -> 1) and orthogonality (a != b -> 0).
		[[nodiscard]] float AngularOverlap(int lA, int mA, int lB, int mB)
		{
			constexpr int kThetaSteps = 400;
			constexpr int kPhiSteps = 800;
			const float pi = std::numbers::pi_v<float>;
			const float thetaStep = pi / static_cast<float>(kThetaSteps);
			const float phiStep = 2.0f * pi / static_cast<float>(kPhiSteps);
			float total = 0.0f;
			for (int thetaIndex = 0; thetaIndex < kThetaSteps; ++thetaIndex)
			{
				const float theta = (static_cast<float>(thetaIndex) + 0.5f) * thetaStep;
				for (int phiIndex = 0; phiIndex < kPhiSteps; ++phiIndex)
				{
					const float phi = (static_cast<float>(phiIndex) + 0.5f) * phiStep;
					const glm::vec3 direction(
						std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
					total += RealSphericalHarmonic(lA, mA, direction) *
						RealSphericalHarmonic(lB, mB, direction) * std::sin(theta) * thetaStep * phiStep;
				}
			}
			return total;
		}

		[[nodiscard]] AtomicOrbital MakeOrbital(int n, int l, int m, float effectiveCharge = 1.0f)
		{
			AtomicOrbital orbital;
			orbital.n = n;
			orbital.l = l;
			orbital.m = m;
			orbital.effectiveCharge = effectiveCharge;
			return orbital;
		}

		[[nodiscard]] OrbitalWavefunction SingleTerm(const AtomicOrbital &orbital)
		{
			OrbitalTerm term;
			term.orbital = orbital;
			OrbitalWavefunction wavefunction;
			wavefunction.terms.push_back(term);
			return wavefunction;
		}
	} // namespace

	// --- radial part -------------------------------------------------------------------------

	TEST(HydrogenicRadialTests, OneSDecaysExponentiallyWithTheBohrRadius)
	{
		const float atOrigin = HydrogenicRadial(1, 0, 1.0f, 0.0f);
		ASSERT_GT(atOrigin, 0.0f);
		EXPECT_NEAR(HydrogenicRadial(1, 0, 1.0f, kA0) / atOrigin, std::exp(-1.0f), 1e-4f);
		EXPECT_NEAR(HydrogenicRadial(1, 0, 1.0f, 2.0f * kA0) / atOrigin, std::exp(-2.0f), 1e-4f);
	}

	TEST(HydrogenicRadialTests, TwoSHasExactlyOneNodeAtTwoBohrRadii)
	{
		EXPECT_GT(HydrogenicRadial(2, 0, 1.0f, 1.0f * kA0), 0.0f);
		EXPECT_NEAR(HydrogenicRadial(2, 0, 1.0f, 2.0f * kA0), 0.0f, 1e-5f);
		EXPECT_LT(HydrogenicRadial(2, 0, 1.0f, 3.0f * kA0), 0.0f);
	}

	TEST(HydrogenicRadialTests, ThreeSHasTwoNodes)
	{
		int signChanges = 0;
		float previous = HydrogenicRadial(3, 0, 1.0f, 0.01f * kA0);
		for (float radius = 0.02f * kA0; radius < 20.0f * kA0; radius += 0.01f * kA0)
		{
			const float current = HydrogenicRadial(3, 0, 1.0f, radius);
			if ((previous < 0.0f) != (current < 0.0f))
				++signChanges;
			previous = current;
		}
		EXPECT_EQ(signChanges, 2);
	}

	TEST(HydrogenicRadialTests, TwoPVanishesAtTheNucleus)
	{
		EXPECT_NEAR(HydrogenicRadial(2, 1, 1.0f, 0.0f), 0.0f, 1e-6f);
		EXPECT_GT(HydrogenicRadial(2, 1, 1.0f, kA0), 0.0f);
	}

	TEST(HydrogenicRadialTests, RadialFunctionsAreNormalised)
	{
		EXPECT_NEAR(RadialNorm(1, 0, 1.0f), 1.0f, 1e-3f);
		EXPECT_NEAR(RadialNorm(2, 0, 1.0f), 1.0f, 1e-3f);
		EXPECT_NEAR(RadialNorm(2, 1, 1.0f), 1.0f, 1e-3f);
		EXPECT_NEAR(RadialNorm(3, 2, 1.0f), 1.0f, 1e-3f);
	}

	TEST(HydrogenicRadialTests, HigherEffectiveChargeContractsTheOrbital)
	{
		// A carbon-like 2p peaks closer to its nucleus than a hydrogen 2p - the whole point of the
		// effectiveCharge knob.
		EXPECT_NEAR(RadialNorm(2, 1, 3.5f), 1.0f, 1e-3f);
		const float hydrogenTail = HydrogenicRadial(2, 1, 1.0f, 6.0f * kA0);
		const float carbonTail = HydrogenicRadial(2, 1, 3.5f, 6.0f * kA0);
		EXPECT_LT(carbonTail, hydrogenTail);
	}

	TEST(HydrogenicRadialTests, InvalidQuantumNumbersReturnZero)
	{
		EXPECT_EQ(HydrogenicRadial(0, 0, 1.0f, kA0), 0.0f);
		EXPECT_EQ(HydrogenicRadial(2, 2, 1.0f, kA0), 0.0f); // l must be < n
		EXPECT_EQ(HydrogenicRadial(2, -1, 1.0f, kA0), 0.0f);
	}

	// --- angular part ------------------------------------------------------------------------

	TEST(RealSphericalHarmonicTests, SIsIsotropic)
	{
		const float reference = RealSphericalHarmonic(0, 0, glm::vec3(0.0f, 0.0f, 1.0f));
		EXPECT_GT(reference, 0.0f);
		EXPECT_NEAR(RealSphericalHarmonic(0, 0, glm::vec3(1.0f, 0.0f, 0.0f)), reference, 1e-6f);
		EXPECT_NEAR(RealSphericalHarmonic(0, 0, glm::vec3(-3.0f, 2.0f, 1.0f)), reference, 1e-6f);
		// The only l for which a zero-length offset still has a defined value.
		EXPECT_NEAR(RealSphericalHarmonic(0, 0, glm::vec3(0.0f)), reference, 1e-6f);
	}

	TEST(RealSphericalHarmonicTests, POrbitalsPointAlongTheirNamedAxes)
	{
		// m = 0 -> p_z, m = +1 -> p_x, m = -1 -> p_y (see AtomicOrbital's convention comment).
		EXPECT_GT(RealSphericalHarmonic(1, 0, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
		EXPECT_NEAR(RealSphericalHarmonic(1, 0, glm::vec3(1.0f, 1.0f, 0.0f)), 0.0f, 1e-6f);

		EXPECT_GT(RealSphericalHarmonic(1, 1, glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f);
		EXPECT_NEAR(RealSphericalHarmonic(1, 1, glm::vec3(0.0f, 1.0f, 1.0f)), 0.0f, 1e-6f);

		EXPECT_GT(RealSphericalHarmonic(1, -1, glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f);
		EXPECT_NEAR(RealSphericalHarmonic(1, -1, glm::vec3(1.0f, 0.0f, 1.0f)), 0.0f, 1e-6f);
	}

	TEST(RealSphericalHarmonicTests, POrbitalsAreAntisymmetric)
	{
		const glm::vec3 direction(0.3f, -0.5f, 0.8f);
		for (const int m : {-1, 0, 1})
		{
			EXPECT_NEAR(
				RealSphericalHarmonic(1, m, direction), -RealSphericalHarmonic(1, m, -direction), 1e-6f);
		}
	}

	TEST(RealSphericalHarmonicTests, DZSquaredIsPositiveOnTheAxisAndNegativeInThePlane)
	{
		EXPECT_GT(RealSphericalHarmonic(2, 0, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
		EXPECT_LT(RealSphericalHarmonic(2, 0, glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f);
		// The cone where 3cos^2(theta) = 1.
		const float magicZ = 1.0f / std::sqrt(3.0f);
		const float magicXY = std::sqrt(1.0f - magicZ * magicZ);
		EXPECT_NEAR(RealSphericalHarmonic(2, 0, glm::vec3(magicXY, 0.0f, magicZ)), 0.0f, 1e-5f);
	}

	TEST(RealSphericalHarmonicTests, DOrbitalsMatchTheirNamedLobes)
	{
		// m = -2 -> d_xy, -1 -> d_yz, +1 -> d_xz, +2 -> d_x2-y2.
		EXPECT_GT(RealSphericalHarmonic(2, -2, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))), 0.0f);
		EXPECT_LT(RealSphericalHarmonic(2, -2, glm::normalize(glm::vec3(-1.0f, 1.0f, 0.0f))), 0.0f);

		EXPECT_GT(RealSphericalHarmonic(2, -1, glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f))), 0.0f);
		EXPECT_GT(RealSphericalHarmonic(2, 1, glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f))), 0.0f);

		EXPECT_GT(RealSphericalHarmonic(2, 2, glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f);
		EXPECT_LT(RealSphericalHarmonic(2, 2, glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f);
	}

	TEST(RealSphericalHarmonicTests, HarmonicsAreOrthonormalOnTheSphere)
	{
		EXPECT_NEAR(AngularOverlap(0, 0, 0, 0), 1.0f, 2e-3f);
		EXPECT_NEAR(AngularOverlap(1, 0, 1, 0), 1.0f, 2e-3f);
		EXPECT_NEAR(AngularOverlap(2, 2, 2, 2), 1.0f, 2e-3f);

		EXPECT_NEAR(AngularOverlap(0, 0, 1, 0), 0.0f, 2e-3f);
		EXPECT_NEAR(AngularOverlap(1, 1, 1, -1), 0.0f, 2e-3f);
		EXPECT_NEAR(AngularOverlap(2, 0, 2, 2), 0.0f, 2e-3f);
	}

	TEST(RealSphericalHarmonicTests, OutOfRangeMomentumReturnsZero)
	{
		EXPECT_EQ(RealSphericalHarmonic(1, 2, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
		EXPECT_EQ(RealSphericalHarmonic(-1, 0, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
		// Direction is undefined at the origin for anything but s.
		EXPECT_EQ(RealSphericalHarmonic(1, 0, glm::vec3(0.0f)), 0.0f);
	}

	// --- full orbital ------------------------------------------------------------------------

	TEST(EvaluateAtomicOrbitalTests, TwoPZIsPositiveAboveAndNegativeBelowItsNodalPlane)
	{
		const AtomicOrbital orbital = MakeOrbital(2, 1, 0);
		const float above = EvaluateAtomicOrbital(orbital, glm::vec3(0.0f, 0.0f, 2.0f * kA0));
		const float below = EvaluateAtomicOrbital(orbital, glm::vec3(0.0f, 0.0f, -2.0f * kA0));
		EXPECT_GT(above, 0.0f);
		EXPECT_NEAR(below, -above, 1e-6f);
		EXPECT_NEAR(EvaluateAtomicOrbital(orbital, glm::vec3(2.0f * kA0, 0.0f, 0.0f)), 0.0f, 1e-6f);
	}

	TEST(EvaluateAtomicOrbitalTests, InvalidOrbitalsEvaluateToZero)
	{
		EXPECT_EQ(EvaluateAtomicOrbital(MakeOrbital(1, 1, 0), glm::vec3(kA0)), 0.0f);
		EXPECT_EQ(EvaluateAtomicOrbital(MakeOrbital(2, 1, 2), glm::vec3(kA0)), 0.0f);
		EXPECT_EQ(EvaluateAtomicOrbital(MakeOrbital(0, 0, 0), glm::vec3(kA0)), 0.0f);
	}

	TEST(EvaluateOrbitalTests, TermsAreSummedAtTheirOwnCentres)
	{
		const AtomicOrbital oneS = MakeOrbital(1, 0, 0);
		OrbitalTerm left;
		left.orbital = oneS;
		left.center = glm::vec3(-1.0f, 0.0f, 0.0f);
		OrbitalTerm right = left;
		right.center = glm::vec3(1.0f, 0.0f, 0.0f);

		OrbitalWavefunction wavefunction;
		wavefunction.terms = {left, right};

		const float single = EvaluateAtomicOrbital(oneS, glm::vec3(0.0f));
		// The midpoint sees one tail from each side, so exactly twice one tail at distance 1.
		const float tail = EvaluateAtomicOrbital(oneS, glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_NEAR(EvaluateOrbital(wavefunction, glm::vec3(0.0f)), 2.0f * tail, 1e-6f);
		// And each nucleus sees its own peak plus the other's tail at distance 2.
		const float farTail = EvaluateAtomicOrbital(oneS, glm::vec3(2.0f, 0.0f, 0.0f));
		EXPECT_NEAR(
			EvaluateOrbital(wavefunction, glm::vec3(-1.0f, 0.0f, 0.0f)), single + farTail, 1e-6f);
	}

	TEST(EvaluateOrbitalTests, NegativeCoefficientFlipsTheLobe)
	{
		OrbitalWavefunction plus = SingleTerm(MakeOrbital(2, 1, 0));
		OrbitalWavefunction minus = plus;
		minus.terms[0].coefficient = -1.0f;
		const glm::vec3 point(0.0f, 0.0f, 2.0f * kA0);
		EXPECT_NEAR(EvaluateOrbital(minus, point), -EvaluateOrbital(plus, point), 1e-6f);
	}

	TEST(EvaluateOrbitalTests, OrientationRotatesTheLobeIntoSceneSpace)
	{
		// A p_z turned onto +x by a -90 degree rotation about y must now peak along x and vanish
		// along z, which is what lets a bond point anywhere.
		OrbitalWavefunction wavefunction = SingleTerm(MakeOrbital(2, 1, 0));
		wavefunction.terms[0].orientation = glm::mat3(
			glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));

		const float alongX = EvaluateOrbital(wavefunction, glm::vec3(2.0f * kA0, 0.0f, 0.0f));
		EXPECT_GT(alongX, 0.0f);
		EXPECT_NEAR(EvaluateOrbital(wavefunction, glm::vec3(0.0f, 0.0f, 2.0f * kA0)), 0.0f, 1e-6f);
		EXPECT_NEAR(EvaluateOrbital(wavefunction, glm::vec3(-2.0f * kA0, 0.0f, 0.0f)), -alongX, 1e-6f);
	}

	TEST(EvaluateOrbitalTests, EmptyWavefunctionIsZeroEverywhere)
	{
		const OrbitalWavefunction empty;
		EXPECT_EQ(EvaluateOrbital(empty, glm::vec3(0.0f)), 0.0f);
		EXPECT_EQ(EvaluateOrbital(empty, glm::vec3(1.0f, 2.0f, 3.0f)), 0.0f);
	}

	// --- sampling ----------------------------------------------------------------------------

	TEST(SampleOrbitalToGridTests, GridMatchesTheRequestedDimensionsAndIsCentredOnTheOrbital)
	{
		OrbitalWavefunction wavefunction = SingleTerm(MakeOrbital(1, 0, 0));
		wavefunction.terms[0].center = glm::vec3(3.0f, -2.0f, 1.0f);

		OrbitalSamplingSettings settings;
		settings.dimensions = glm::ivec3(17, 19, 21);
		settings.extent = 4.0f;
		const OrbitalGridData grid = SampleOrbitalToGrid(wavefunction, settings);

		EXPECT_EQ(grid.dimensions, settings.dimensions);
		EXPECT_EQ(grid.values.size(), 17u * 19u * 21u);
		// A cube of side 2 * extent, with the orbital centre at its middle.
		EXPECT_NEAR(grid.cell[0].x, 8.0f, 1e-4f);
		EXPECT_NEAR(grid.cell[1].y, 8.0f, 1e-4f);
		EXPECT_NEAR(grid.cell[2].z, 8.0f, 1e-4f);
		EXPECT_NEAR(grid.origin.x, -1.0f, 1e-4f);
		EXPECT_NEAR(grid.origin.y, -6.0f, 1e-4f);
		EXPECT_NEAR(grid.origin.z, -3.0f, 1e-4f);
		// Analytic orbitals carry no calculated energy - see the header.
		EXPECT_EQ(grid.energy, 0.0f);
		EXPECT_EQ(grid.occupation, 0.0f);
	}

	TEST(SampleOrbitalToGridTests, SampledValuesMatchTheAnalyticWavefunction)
	{
		OrbitalWavefunction wavefunction = SingleTerm(MakeOrbital(2, 1, 0));
		OrbitalSamplingSettings settings;
		settings.dimensions = glm::ivec3(9);
		settings.extent = 3.0f;
		const OrbitalGridData grid = SampleOrbitalToGrid(wavefunction, settings);

		// C-order, x slowest / z fastest (OrbitalGridData's own documented layout).
		const auto valueAt = [&](int x, int y, int z) {
			return grid.values[static_cast<std::size_t>((x * 9 + y) * 9 + z)];
		};
		for (const glm::ivec3 sample : {glm::ivec3(0, 0, 0), glm::ivec3(4, 4, 7), glm::ivec3(8, 3, 1)})
		{
			const glm::vec3 fractional = glm::vec3(sample) / 8.0f;
			const glm::vec3 world = grid.origin + grid.cell[0] * fractional.x +
				grid.cell[1] * fractional.y + grid.cell[2] * fractional.z;
			EXPECT_NEAR(valueAt(sample.x, sample.y, sample.z), EvaluateOrbital(wavefunction, world), 1e-6f);
		}
	}

	TEST(SampleOrbitalToGridTests, AutomaticExtentContainsTheOrbital)
	{
		// A diffuse 3d needs a much bigger box than a tight 1s; both must come back with their
		// amplitude decayed at the wall rather than clipped.
		for (const AtomicOrbital orbital : {MakeOrbital(1, 0, 0), MakeOrbital(3, 2, 0)})
		{
			const OrbitalWavefunction wavefunction = SingleTerm(orbital);
			OrbitalSamplingSettings settings;
			settings.dimensions = glm::ivec3(33);
			settings.extent = 0.0f; // ask for the automatic size
			const OrbitalGridData grid = SampleOrbitalToGrid(wavefunction, settings);

			const float extent = SuggestOrbitalExtent(wavefunction);
			EXPECT_GT(extent, 0.0f);
			EXPECT_NEAR(grid.cell[0].x, 2.0f * extent, 1e-3f);

			float peak = 0.0f;
			for (const float value : grid.values)
				peak = std::max(peak, std::abs(value));
			ASSERT_GT(peak, 0.0f);
			// Corner of the box, the farthest sampled point from the centre.
			EXPECT_LT(std::abs(grid.values.front()), 0.02f * peak);
		}
		EXPECT_GT(
			SuggestOrbitalExtent(SingleTerm(MakeOrbital(3, 2, 0))),
			SuggestOrbitalExtent(SingleTerm(MakeOrbital(1, 0, 0))));
	}

	TEST(SampleOrbitalToGridTests, BoxGrowsToCoverEveryCentre)
	{
		// A two-centre orbital has to fit both atoms plus their tails, not just the centroid.
		OrbitalTerm left;
		left.orbital = MakeOrbital(2, 1, 0);
		left.center = glm::vec3(-5.0f, 0.0f, 0.0f);
		OrbitalTerm right = left;
		right.center = glm::vec3(5.0f, 0.0f, 0.0f);
		OrbitalWavefunction wavefunction;
		wavefunction.terms = {left, right};

		EXPECT_EQ(OrbitalCentroid(wavefunction), glm::vec3(0.0f));
		EXPECT_GT(SuggestOrbitalExtent(wavefunction), 5.0f);
	}

	TEST(SampleOrbitalToGridTests, DegenerateRequestsProduceAnEmptyGrid)
	{
		const OrbitalWavefunction wavefunction = SingleTerm(MakeOrbital(1, 0, 0));
		OrbitalSamplingSettings settings;
		settings.dimensions = glm::ivec3(1, 8, 8); // needs at least 2 samples per axis to span a cell
		EXPECT_TRUE(SampleOrbitalToGrid(wavefunction, settings).values.empty());

		settings.dimensions = glm::ivec3(8);
		EXPECT_TRUE(SampleOrbitalToGrid(OrbitalWavefunction{}, settings).values.empty());
	}

	TEST(SuggestOrbitalIsoValueTests, ScalesWithTheGridPeak)
	{
		OrbitalGridData grid;
		grid.dimensions = glm::ivec3(2);
		grid.values = {0.0f, 0.4f, -2.0f, 0.1f, 0.0f, 1.0f, -0.3f, 0.2f};
		EXPECT_NEAR(SuggestOrbitalIsoValue(grid, 0.2f), 0.4f, 1e-6f);
		EXPECT_NEAR(SuggestOrbitalIsoValue(grid, 0.5f), 1.0f, 1e-6f);
	}

	TEST(SuggestOrbitalIsoValueTests, EmptyOrFlatGridHasNoIsoValue)
	{
		EXPECT_EQ(SuggestOrbitalIsoValue(OrbitalGridData{}, 0.2f), 0.0f);
		OrbitalGridData flat;
		flat.dimensions = glm::ivec3(2);
		flat.values.assign(8, 0.0f);
		EXPECT_EQ(SuggestOrbitalIsoValue(flat, 0.2f), 0.0f);
	}
} // namespace DefectStudio::Tests
