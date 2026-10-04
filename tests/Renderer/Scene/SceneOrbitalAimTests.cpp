#include <gtest/gtest.h>

#include <optional>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Scene/SceneOrbitalAim.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// The frame BuildOrbitalWavefunction applies for rotationEuler (SceneOrbitalGeometry.cpp).
		[[nodiscard]] glm::mat3 Frame(const glm::vec3 &eulerDegrees)
		{
			return glm::mat3_cast(glm::quat(glm::radians(eulerDegrees)));
		}

		[[nodiscard]] RendererWindowState::SceneOrbital Orbital(OrbitalPreset preset, int lobe, const glm::vec3 &euler = glm::vec3(0.0f))
		{
			RendererWindowState::SceneOrbital orbital;
			orbital.preset = preset;
			orbital.lobeIndex = lobe;
			orbital.rotationEuler = euler;
			return orbital;
		}

		void ExpectAimed(const RendererWindowState::SceneOrbital &orbital, const glm::vec3 &euler, const glm::vec3 &direction)
		{
			const glm::vec3 axis = Frame(euler) * *OrbitalPresetMemberAxis(orbital.preset, orbital.lobeIndex);
			EXPECT_NEAR(glm::length(axis - glm::normalize(direction)), 0.0f, 1e-4f)
				<< "axis (" << axis.x << ", " << axis.y << ", " << axis.z << ")";
		}

		// Vacancy at the origin, nitrogen and three carbons around it - the NV- picture.
		[[nodiscard]] RendererWindowState NvWindow()
		{
			RendererWindowState window;
			const glm::vec3 positions[] = {
				glm::vec3(0.0f, 0.0f, 1.6f), glm::vec3(1.5f, 0.0f, -0.5f),
				glm::vec3(-0.75f, 1.3f, -0.5f), glm::vec3(-0.75f, -1.3f, -0.5f)};
			const char *elements[] = {"N", "C", "C", "C"};
			for (int i = 0; i < 4; ++i)
			{
				RendererAtomData atom;
				atom.element = elements[i];
				atom.cartesianPosition = positions[i];
				window.structure.atoms.push_back(atom);
			}
			RendererVacancyData vacancy;
			vacancy.label = "V_C";
			vacancy.cartesianPosition = glm::vec3(0.0f);
			window.structure.vacancies.push_back(vacancy);
			return window;
		}
	} // namespace

	TEST(SceneOrbitalAimTests, PzIsTurnedOntoTheTarget)
	{
		const RendererWindowState::SceneOrbital orbital = Orbital(OrbitalPreset::P, 0);
		const glm::vec3 centre(1.0f, 2.0f, 3.0f);
		const glm::vec3 target(2.0f, 3.0f, 3.0f);

		const std::optional<glm::vec3> euler = AimSceneOrbitalEuler(orbital, centre, target);

		ASSERT_TRUE(euler.has_value());
		ExpectAimed(orbital, *euler, target - centre);
	}

	TEST(SceneOrbitalAimTests, EveryTetrahedralLobeCanBeAimed)
	{
		const glm::vec3 direction(-0.3f, 0.8f, 0.2f);
		for (int lobe = 0; lobe < 4; ++lobe)
		{
			const RendererWindowState::SceneOrbital orbital = Orbital(OrbitalPreset::Sp3, lobe, glm::vec3(10.0f, -20.0f, 35.0f));
			const std::optional<glm::vec3> euler = AimSceneOrbitalEuler(orbital, glm::vec3(0.0f), direction);
			ASSERT_TRUE(euler.has_value()) << "lobe " << lobe;
			ExpectAimed(orbital, *euler, direction);
		}
	}

	TEST(SceneOrbitalAimTests, AntiparallelTargetIsAHalfTurnNotANaN)
	{
		const RendererWindowState::SceneOrbital orbital = Orbital(OrbitalPreset::Sp3, 0);

		const std::optional<glm::vec3> euler = AimSceneOrbitalEuler(orbital, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -2.0f));

		ASSERT_TRUE(euler.has_value());
		EXPECT_FALSE(glm::any(glm::isnan(*euler)));
		ExpectAimed(orbital, *euler, glm::vec3(0.0f, 0.0f, -1.0f));
	}

	// Minimal rotation on top of the current orientation: an orbital already pointing at the target
	// keeps its frame, twist about the lobe included.
	TEST(SceneOrbitalAimTests, AlreadyAimedOrbitalKeepsItsFrame)
	{
		const glm::vec3 start(0.0f, 0.0f, 40.0f); // twisted about z, p_z still along +z
		const RendererWindowState::SceneOrbital orbital = Orbital(OrbitalPreset::P, 0, start);

		const std::optional<glm::vec3> euler = AimSceneOrbitalEuler(orbital, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 3.0f));

		ASSERT_TRUE(euler.has_value());
		const glm::mat3 before = Frame(start);
		const glm::mat3 after = Frame(*euler);
		for (int column = 0; column < 3; ++column)
			EXPECT_NEAR(glm::length(before[column] - after[column]), 0.0f, 1e-4f);
	}

	TEST(SceneOrbitalAimTests, NothingToAimGivesNullopt)
	{
		EXPECT_FALSE(AimSceneOrbitalEuler(Orbital(OrbitalPreset::S, 0), glm::vec3(0.0f), glm::vec3(1.0f)).has_value());
		EXPECT_FALSE(AimSceneOrbitalEuler(Orbital(OrbitalPreset::Sigma, 0), glm::vec3(0.0f), glm::vec3(1.0f)).has_value());
		EXPECT_FALSE(AimSceneOrbitalEuler(Orbital(OrbitalPreset::P, 0), glm::vec3(1.0f), glm::vec3(1.0f)).has_value());
	}

	TEST(SceneOrbitalAimTests, TargetsAreVacanciesThenSelectedAtomsThenTheCursor)
	{
		RendererWindowState window = NvWindow();
		window.selectedAtomIndices = {2, 0, 99};
		window.cursor3DPosition = glm::vec3(5.0f, 0.0f, 0.0f);

		const std::vector<OrbitalAimTarget> targets = CollectOrbitalAimTargets(window);

		ASSERT_EQ(targets.size(), 4u);
		EXPECT_EQ(targets[0].label, "V_C #1");
		EXPECT_EQ(targets[0].position, glm::vec3(0.0f));
		EXPECT_EQ(targets[1].label, "C #3");
		EXPECT_EQ(targets[1].position, window.structure.atoms[2].cartesianPosition);
		EXPECT_EQ(targets[2].label, "N #1");
		EXPECT_EQ(targets[3].label, "Kursor 3D");
		EXPECT_EQ(targets[3].position, glm::vec3(5.0f, 0.0f, 0.0f));
	}

	TEST(SceneOrbitalAimTests, DanglingBondTargetIsTheNearestVacancyElseTheCentroid)
	{
		RendererWindowState window = NvWindow();
		RendererVacancyData far;
		far.cartesianPosition = glm::vec3(20.0f, 0.0f, 0.0f);
		window.structure.vacancies.push_back(far);

		const std::optional<glm::vec3> withVacancy = ResolveDanglingBondTarget(window, {0, 1, 2, 3});
		ASSERT_TRUE(withVacancy.has_value());
		EXPECT_EQ(*withVacancy, glm::vec3(0.0f));

		window.structure.vacancies.clear();
		const std::optional<glm::vec3> centroid = ResolveDanglingBondTarget(window, {1, 2, 3});
		ASSERT_TRUE(centroid.has_value());
		EXPECT_NEAR(glm::length(*centroid - glm::vec3(0.0f, 0.0f, -0.5f)), 0.0f, 1e-5f);

		EXPECT_FALSE(ResolveDanglingBondTarget(window, {42}).has_value());
	}

	TEST(SceneOrbitalAimTests, DanglingBondsPointEachNeighbourIntoTheVacancy)
	{
		const RendererWindowState window = NvWindow();

		const std::vector<RendererWindowState::SceneOrbital> orbitals =
			MakeDanglingBondOrbitals(window, {0, 1, 2, 3}, glm::vec3(0.0f));

		ASSERT_EQ(orbitals.size(), 4u);
		for (std::size_t i = 0; i < orbitals.size(); ++i)
		{
			const RendererWindowState::SceneOrbital &orbital = orbitals[i];
			EXPECT_EQ(orbital.preset, OrbitalPreset::Sp3);
			EXPECT_EQ(orbital.lobeIndex, 0);
			ASSERT_EQ(orbital.anchorAtoms.size(), 1u);
			EXPECT_EQ(orbital.anchorAtoms[0], i);
			ExpectAimed(orbital, orbital.rotationEuler, -window.structure.atoms[i].cartesianPosition);
		}
		EXPECT_GT(orbitals[1].effectiveCharge, 1.0f); // sized as carbon, not hydrogen
	}

	TEST(SceneOrbitalAimTests, MultiAimUsesEachAnchoredCentreAndLeavesTheInputUnchanged)
	{
		RendererWindowState window = NvWindow();
		for (std::size_t i = 1; i < 4; ++i)
		{
			auto orbital = Orbital(OrbitalPreset::P, 0, glm::vec3(10, 20, 30));
			orbital.id = SceneObjectId{100 + i};
			orbital.centerA = glm::vec3(99.0f); // the anchor, not this stale value, is the centre
			orbital.anchorAtoms = {i};
			window.sceneOrbitals.push_back(orbital);
			window.selectedSceneOrbitals.push_back(orbital.id);
		}
		const auto result = AimSelectedSceneOrbitals(window, window.selectedSceneOrbitals, glm::vec3(0.0f));
		ASSERT_EQ(result.size(), 3u);
		for (std::size_t i = 0; i < result.size(); ++i)
		{
			EXPECT_EQ(result[i].id, window.sceneOrbitals[i].id);
			ExpectAimed(window.sceneOrbitals[i], result[i].eulerDegrees, -window.structure.atoms[i + 1].cartesianPosition);
			EXPECT_EQ(window.sceneOrbitals[i].rotationEuler, glm::vec3(10, 20, 30));
		}
	}

	TEST(SceneOrbitalAimTests, MultiAimSkipsUnsupportedCoincidentAndStaleSelection)
	{
		RendererWindowState window;
		const OrbitalPreset presets[] = {OrbitalPreset::P, OrbitalPreset::S, OrbitalPreset::Sigma, OrbitalPreset::P};
		for (std::size_t i = 0; i < 4; ++i)
		{
			auto orbital = Orbital(presets[i], 0);
			orbital.id = SceneObjectId{100 + i};
			window.sceneOrbitals.push_back(orbital);
		}
		window.sceneOrbitals[3].lcaoComponents.emplace_back();
		const std::vector<SceneObjectId> ids = {SceneObjectId{100}, SceneObjectId{101}, SceneObjectId{102}, SceneObjectId{103}, SceneObjectId{999}};
		const auto result = AimSelectedSceneOrbitals(window, ids, glm::vec3(0, 1, 0));
		ASSERT_EQ(result.size(), 1u);
		EXPECT_EQ(result.front().id, SceneObjectId{100});
		EXPECT_TRUE(AimSelectedSceneOrbitals(window, ids, glm::vec3(0.0f)).empty());
		EXPECT_TRUE(AimSelectedSceneOrbitals(window, {}, glm::vec3(0, 1, 0)).empty());
	}

	TEST(SceneOrbitalAimTests, NearestVacancyIsResolvedPerCentreAndDuplicateIdsAreNotRepeated)
	{
		RendererWindowState window;
		RendererVacancyData vacancy;
		vacancy.cartesianPosition = glm::vec3(-2, 0, 0);
		window.structure.vacancies.push_back(vacancy);
		vacancy.cartesianPosition = glm::vec3(12, 0, 0);
		window.structure.vacancies.push_back(vacancy);
		auto a = Orbital(OrbitalPreset::P, 0);
		a.id = SceneObjectId{1};
		auto b = a;
		b.id = SceneObjectId{2};
		b.centerA = glm::vec3(10, 0, 0);
		window.sceneOrbitals = {a, b};
		const std::vector<SceneObjectId> ids = {a.id, b.id, a.id};
		const auto result = AimSelectedSceneOrbitals(window, ids, std::nullopt);
		ASSERT_EQ(result.size(), 2u);
		ExpectAimed(a, result[0].eulerDegrees, glm::vec3(-1, 0, 0));
		ExpectAimed(b, result[1].eulerDegrees, glm::vec3(1, 0, 0));
		window.structure.vacancies.clear();
		EXPECT_TRUE(AimSelectedSceneOrbitals(window, ids, std::nullopt).empty());
	}
} // namespace DefectStudio::Tests
