#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState MakeWindow(bool reversed = false)
		{
			RendererWindowState window;
			std::vector<std::pair<const char *, glm::vec3>> atoms = {
				{"N", glm::vec3(0.0f, 0.0f, 1.6f)}, {"C", glm::vec3(1.5f, 0.0f, -0.5f)},
				{"C", glm::vec3(-0.75f, 1.3f, -0.5f)}};
			if (reversed)
				std::reverse(atoms.begin(), atoms.end());
			for (const auto &[element, position] : atoms)
			{
				RendererAtomData atom;
				atom.element = element;
				atom.cartesianPosition = position;
				window.structure.atoms.push_back(atom);
			}
			return window;
		}

		[[nodiscard]] RendererWindowState::SceneOrbital MakeLcao(const RendererWindowState &window)
		{
			RendererWindowState::SceneOrbital orbital;
			orbital.displayName = "e_x (E #1)";
			orbital.phaseFlipped = true;
			orbital.isoFraction = 0.15f;
			for (std::size_t i = 0; i < window.structure.atoms.size(); ++i)
			{
				RendererWindowState::SceneOrbital::LcaoComponent component;
				component.anchorAtom = i;
				component.center = window.structure.atoms[i].cartesianPosition;
				component.preset = i == 0 ? OrbitalPreset::P : OrbitalPreset::Sp3;
				component.shell = 2;
				component.lobeIndex = 0;
				component.effectiveCharge = 3.25f + static_cast<float>(i);
				component.rotationEuler = glm::vec3(10.0f * static_cast<float>(i), -20.0f, 5.0f);
				component.coefficient = i == 1 ? -0.5f : 0.75f;
				orbital.lcaoComponents.push_back(component);
			}
			return orbital;
		}

		[[nodiscard]] SceneObjectsFile RoundTrip(const RendererWindowState &source)
		{
			SceneObjectsFile file;
			file.structures.push_back({"structure", ExtractPersistedSceneObjects(source)});
			SceneObjectsFile parsed;
			std::vector<StructuredError> warnings;
			std::string error;
			EXPECT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), parsed, warnings, error)) << error;
			EXPECT_TRUE(warnings.empty());
			return parsed;
		}
	} // namespace

	TEST(SceneOrbitalLcaoPersistenceTests, LcaoOrbitalSurvivesTheProjectFile)
	{
		RendererWindowState source = MakeWindow();
		const RendererWindowState::SceneOrbital orbital = MakeLcao(source);
		source.sceneOrbitals.push_back(orbital);

		const SceneObjectsFile parsed = RoundTrip(source);
		RendererWindowState target = MakeWindow();
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(target, parsed.structures.at(0).objects, warnings);

		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(target.sceneOrbitals.size(), 1u);
		const RendererWindowState::SceneOrbital &loaded = target.sceneOrbitals[0];
		EXPECT_EQ(loaded.displayName, "e_x (E #1)");
		EXPECT_TRUE(loaded.phaseFlipped);
		EXPECT_FLOAT_EQ(loaded.isoFraction, 0.15f);
		ASSERT_EQ(loaded.lcaoComponents.size(), 3u);
		for (std::size_t i = 0; i < 3; ++i)
		{
			const auto &expected = orbital.lcaoComponents[i];
			const auto &got = loaded.lcaoComponents[i];
			EXPECT_EQ(got.anchorAtom, expected.anchorAtom);
			EXPECT_EQ(got.center, expected.center);
			EXPECT_EQ(got.preset, expected.preset);
			EXPECT_EQ(got.shell, expected.shell);
			EXPECT_EQ(got.lobeIndex, expected.lobeIndex);
			EXPECT_FLOAT_EQ(got.effectiveCharge, expected.effectiveCharge);
			EXPECT_NEAR(glm::length(got.rotationEuler - expected.rotationEuler), 0.0f, 1e-4f);
			EXPECT_FLOAT_EQ(got.coefficient, expected.coefficient);
		}
	}

	// Component anchors are atom references like every other anchor: a reordered structure file
	// rebinds them by element + position rather than by stale index.
	TEST(SceneOrbitalLcaoPersistenceTests, ComponentAnchorsRebindAfterAtomsAreReordered)
	{
		RendererWindowState source = MakeWindow();
		source.sceneOrbitals.push_back(MakeLcao(source));

		const SceneObjectsFile parsed = RoundTrip(source);
		RendererWindowState target = MakeWindow(true);
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(target, parsed.structures.at(0).objects, warnings);

		ASSERT_EQ(target.sceneOrbitals.size(), 1u);
		ASSERT_EQ(target.sceneOrbitals[0].lcaoComponents.size(), 3u);
		EXPECT_EQ(target.sceneOrbitals[0].lcaoComponents[0].anchorAtom, 2u); // N moved to the end
		EXPECT_EQ(target.sceneOrbitals[0].lcaoComponents[2].anchorAtom, 0u);
	}

	TEST(SceneOrbitalLcaoPersistenceTests, PlainOrbitalWritesNoLcaoKeys)
	{
		RendererWindowState source = MakeWindow();
		RendererWindowState::SceneOrbital plain;
		plain.preset = OrbitalPreset::P;
		source.sceneOrbitals.push_back(plain);

		SceneObjectsFile file;
		file.structures.push_back({"structure", ExtractPersistedSceneObjects(source)});
		const std::string text = SceneObjectsIO::Serialize(file);

		EXPECT_EQ(text.find("lcaoComponents"), std::string::npos);
		EXPECT_EQ(text.find("displayName"), std::string::npos);
	}
} // namespace DefectStudio::Tests
