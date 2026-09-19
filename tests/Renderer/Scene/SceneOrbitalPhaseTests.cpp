#include <gtest/gtest.h>

#include <cmath>

#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState::SceneOrbital MakeOrbital(const OrbitalPreset preset)
		{
			RendererWindowState::SceneOrbital orbital;
			orbital.preset = preset;
			return orbital;
		}
	} // namespace

	TEST(SceneOrbitalPhaseTests, FlipNegatesEveryWavefunctionTerm)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::Sp3);
		const OrbitalWavefunction plain = BuildOrbitalWavefunction(orbital, structure);
		orbital.phaseFlipped = true;
		const OrbitalWavefunction flipped = BuildOrbitalWavefunction(orbital, structure);

		ASSERT_GT(plain.terms.size(), 1u);
		ASSERT_EQ(flipped.terms.size(), plain.terms.size());
		for (std::size_t index = 0; index < plain.terms.size(); ++index)
			EXPECT_FLOAT_EQ(flipped.terms[index].coefficient, -plain.terms[index].coefficient);
	}

	TEST(SceneOrbitalPhaseTests, FlipNegatesTheEvaluatedWavefunction)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::P);
		const glm::vec3 probe(0.0f, 0.0f, 1.0f);
		const float plainValue = EvaluateOrbital(BuildOrbitalWavefunction(orbital, structure), probe);

		orbital.phaseFlipped = true;
		const float flippedValue = EvaluateOrbital(BuildOrbitalWavefunction(orbital, structure), probe);

		ASSERT_GT(std::abs(plainValue), 1e-6f);
		EXPECT_NEAR(flippedValue, -plainValue, 1e-6f);
	}

	TEST(SceneOrbitalPhaseTests, FlipChangesTheMeshCacheKey)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital plain = MakeOrbital(OrbitalPreset::P);
		RendererWindowState::SceneOrbital flipped = plain;
		flipped.phaseFlipped = true;

		EXPECT_NE(MakeSceneOrbitalMeshKey(flipped, structure), MakeSceneOrbitalMeshKey(plain, structure));
	}
} // namespace DefectStudio::Tests
