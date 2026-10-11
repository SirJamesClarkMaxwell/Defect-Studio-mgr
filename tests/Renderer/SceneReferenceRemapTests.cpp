#include "Core/dspch.hpp"

#include <gtest/gtest.h>
#include <limits>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneReferenceRemap.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererWindowState WindowWithAtoms()
		{
			RendererWindowState window;
			window.structure.atoms.resize(4);
			for (std::size_t i = 0; i < 4; ++i)
			{
				window.structure.atoms[i].cartesianPosition = glm::vec3(static_cast<float>(i + 1), 0, 0);
				window.structure.atoms[i].element = "C";
			}
			return window;
		}

		ScenePath Line()
		{
			ScenePath path;
			path.nodes = {{PathElementId{1}, glm::vec3(1), {}}, {PathElementId{2}, glm::vec3(2), {}}};
			path.segments = {{PathElementId{3}, LineSegmentData{}}};
			path.nextElementId = 4;
			return path;
		}

		void ExpectPosition(glm::vec3 actual, glm::vec3 expected)
		{
			EXPECT_NEAR(actual.x, expected.x, 1e-5f);
			EXPECT_NEAR(actual.y, expected.y, 1e-5f);
			EXPECT_NEAR(actual.z, expected.z, 1e-5f);
		}
	}

	TEST(SceneReferenceRemapTests, BatchRemovalShiftsSurvivorsAndUsesReverseAppendVacancyOrder)
	{
		auto window = WindowWithAtoms();
		auto path = Line();
		path.nodes[0].binding.value = PathBinding::CopyPosition{1, glm::vec3(0, 2, 0), 0.9f};
		path.nodes[1].binding.value = PathBinding::CopyPosition{3, {}, 0};
		const auto id = SceneSystem::AppendScenePath(window, path);
		RendererWindowState::FreeLabel label;
		label.anchorAtom = 0;
		window.freeLabels.push_back(label);
		label.anchorAtom = 3;
		window.freeLabels.push_back(label);
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {2, 3};
		window.pinnedMeasurements.push_back(pin);
		pin.linkBroken = true;
		pin.atomIndices = {0, 1};
		pin.frozenAtomPositions = {glm::vec3(8), glm::vec3(9)};
		pin.frozenAtomElements = {"Si", "Ge"};
		window.pinnedMeasurements.push_back(pin);
		RemapSceneAtomReferencesAfterAtomRemoval(window, {0, 1}, {5, 4});
		const auto &remapped = *window.paths->Store().Find(id);
		const auto binding = std::get<PathBinding::CopyVacancy>(remapped.nodes[0].binding.value);
		EXPECT_EQ(binding.vacancyIndex, 4u);
		EXPECT_EQ(binding.offset, glm::vec3(0, 2, 0));
		EXPECT_FLOAT_EQ(binding.buffer, 0.0f);
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(remapped.nodes[1].binding.value).atomIndex, 1u);
		EXPECT_EQ(window.freeLabels[0].anchorVacancy, 5u);
		EXPECT_EQ(window.freeLabels[1].anchorAtom, 1u);
		EXPECT_EQ(window.pinnedMeasurements[0].atomIndices, (std::vector<std::size_t>{0, 1}));
		EXPECT_FALSE(window.pinnedMeasurements[0].linkBroken);
		EXPECT_EQ(window.pinnedMeasurements[1].atomIndices, pin.atomIndices);
		EXPECT_EQ(window.pinnedMeasurements[1].frozenAtomPositions, pin.frozenAtomPositions);
		EXPECT_EQ(window.pinnedMeasurements[1].frozenAtomElements, pin.frozenAtomElements);
	}

	TEST(SceneReferenceRemapTests, MidpointAndBondFrameFreezeAtResolvedPlacement)
	{
		auto window = WindowWithAtoms();
		auto path = Line();
		path.transform.position = glm::vec3(10, 20, 30);
		path.transform.scale = glm::vec3(2, 3, 4);
		path.transformBinding.value = PathTransformBinding::BondFrame{0, 3, 0.4f};
		path.nodes[0].binding.value = PathBinding::BondMidpoint{1, 2, glm::vec3(0, 2, 0)};
		CubicBezierSegmentData curve;
		curve.startHandle.offset = glm::vec3(1, 2, 3);
		curve.endHandle.offset = glm::vec3(2, 3, 4);
		path.segments.front().data = curve;
		const auto id = SceneSystem::AppendScenePath(window, path);
		const auto context = SceneSystem::MakePathBindingContext(window);
		const auto before = ResolveNodePositions(*window.paths->Store().Find(id), context);
		const auto transform = ResolvePathTransform(*window.paths->Store().Find(id), context);
		RemapSceneAtomReferencesAfterAtomRemoval(window, {0, 1}, {1, 0});
		const auto &after = *window.paths->Store().Find(id);
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(after.nodes[0].binding.value));
		EXPECT_TRUE(std::holds_alternative<PathTransformBinding::Free>(after.transformBinding.value));
		EXPECT_EQ(after.transform.position, transform.position);
		EXPECT_EQ(after.transform.rotation, transform.rotation);
		EXPECT_EQ(after.transform.scale, transform.scale);
		const auto positions = ResolveNodePositions(after, context);
		for (std::size_t i = 0; i < positions.positions.size(); ++i) ExpectPosition(positions.positions[i], before.positions[i]);
		for (std::size_t i = 0; i < positions.handlePositions.size(); ++i)
			ExpectPosition(positions.handlePositions[i], before.handlePositions[i]);
	}

	TEST(SceneReferenceRemapTests, SurvivingMidpointAndBondFrameShiftBothAtomIndices)
	{
		auto window = WindowWithAtoms();
		auto path = Line();
		path.transformBinding.value = PathTransformBinding::BondFrame{2, 3, 0.4f};
		path.nodes[0].binding.value = PathBinding::BondMidpoint{2, 3, glm::vec3(0, 2, 0)};
		const auto id = SceneSystem::AppendScenePath(window, path);
		RemapSceneAtomReferencesAfterAtomRemoval(window, {0, 1}, {1, 0});
		const auto &after = *window.paths->Store().Find(id);
		const auto frame = std::get<PathTransformBinding::BondFrame>(after.transformBinding.value);
		const auto midpoint = std::get<PathBinding::BondMidpoint>(after.nodes[0].binding.value);
		EXPECT_EQ(frame.atomA, 0u);
		EXPECT_EQ(frame.atomB, 1u);
		EXPECT_FLOAT_EQ(frame.rollRadians, 0.4f);
		EXPECT_EQ(midpoint.atomA, 0u);
		EXPECT_EQ(midpoint.atomB, 1u);
		EXPECT_EQ(midpoint.offset, glm::vec3(0, 2, 0));
	}

	TEST(SceneReferenceRemapTests, AnchoredObjectsKeepResolvedGeometryWhenTheirLastAnchorsDisappear)
	{
		auto window = WindowWithAtoms();
		RendererWindowState::SceneOrbital orbital;
		orbital.anchorAtoms = {0, 1};
		window.sceneOrbitals.push_back(orbital);
		RendererWindowState::SceneOrbital lcao;
		lcao.lcaoComponents.resize(2);
		lcao.lcaoComponents[0].anchorAtom = 1;
		lcao.lcaoComponents[1].anchorAtom = 3;
		window.sceneOrbitals.push_back(lcao);
		RendererWindowState::ScenePlane plane;
		plane.anchorAtoms = {0, 1};
		plane.halfExtents = glm::vec2(7, 8);
		window.scenePlanes.push_back(plane);
		SceneHideVolume volume;
		volume.anchorAtoms = {0, 1};
		window.sceneHideVolumes.push_back(volume);
		ResolveAnchoredScenePlanes(window);
		const auto expectedPlane = window.scenePlanes.front();
		window.scenePlanes.front().center = glm::vec3(99);
		RemapSceneAtomReferencesAfterAtomRemoval(window, {0, 1}, {1, 0});
		const auto &freeOrbital = window.sceneOrbitals.front();
		EXPECT_TRUE(freeOrbital.anchorAtoms.empty());
		EXPECT_EQ(freeOrbital.centerA, glm::vec3(1, 0, 0));
		EXPECT_EQ(freeOrbital.centerB, glm::vec3(2, 0, 0));
		const auto &components = window.sceneOrbitals[1].lcaoComponents;
		EXPECT_EQ(components[0].anchorAtom, std::numeric_limits<std::size_t>::max());
		EXPECT_EQ(components[0].center, glm::vec3(2, 0, 0));
		EXPECT_EQ(components[1].anchorAtom, 1u);
		EXPECT_EQ(components[1].center, glm::vec3(4, 0, 0));
		EXPECT_TRUE(window.scenePlanes.front().anchorAtoms.empty());
		EXPECT_EQ(window.scenePlanes.front().center, expectedPlane.center);
		EXPECT_EQ(window.scenePlanes.front().normal, expectedPlane.normal);
		EXPECT_EQ(window.scenePlanes.front().tangent, expectedPlane.tangent);
		EXPECT_EQ(window.scenePlanes.front().halfExtents, expectedPlane.halfExtents);
		EXPECT_TRUE(window.sceneHideVolumes.front().anchorAtoms.empty());
		EXPECT_EQ(window.sceneHideVolumes.front().center, glm::vec3(1.5f, 0, 0));
	}

	TEST(SceneReferenceRemapTests, RemovedVacancyUnderCollapsedTransformBakesFiniteWorldGeometry)
	{
		auto window = WindowWithAtoms();
		window.structure.vacancies.resize(1);
		window.structure.vacancies.front().cartesianPosition = glm::vec3(5, 6, 7);
		auto path = Line();
		path.transform.position = glm::vec3(2, 3, 4);
		path.transform.scale = glm::vec3(0, 2, 3);
		path.nodes[0].binding.value = PathBinding::CopyVacancy{0, {}, 0};
		CubicBezierSegmentData curve;
		curve.startHandle.offset = glm::vec3(1, 2, 3);
		curve.endHandle.offset = glm::vec3(2, 3, 4);
		path.segments.front().data = curve;
		const auto id = SceneSystem::AppendScenePath(window, path);
		const auto context = SceneSystem::MakePathBindingContext(window);
		const auto before = ResolveNodePositions(*window.paths->Store().Find(id), context);
		RemapSceneVacancyReferencesAfterVacancyRemoval(window, {0});
		const auto &frozen = *window.paths->Store().Find(id);
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(frozen.nodes[0].binding.value));
		const auto after = ResolveNodePositions(frozen, context);
		EXPECT_EQ(after.positions, before.positions);
		EXPECT_EQ(after.handlePositions, before.handlePositions);
	}

	TEST(SceneReferenceRemapTests, DisplacementArrowsDropRemovedReferencesAndKeepComparisonIndices)
	{
		auto window = WindowWithAtoms();
		window.displacementComparison.emplace();
		auto &result = window.displacementComparison->result;
		result.matches = {AtomDisplacement{0, 9}, AtomDisplacement{3, 8}};
		result.unmatchedReferenceAtomIndices = {1, 2};
		RemapSceneAtomReferencesAfterAtomRemoval(window, {0, 1}, {1, 0});
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_EQ(result.matches.front().referenceAtomIndex, 1u);
		EXPECT_EQ(result.matches.front().comparisonAtomIndex, 8u);
		EXPECT_EQ(result.unmatchedReferenceAtomIndices, (std::vector<std::size_t>{0}));
	}

	TEST(SceneReferenceRemapTests, SameVacancyEndpointsRemovePathAndItsSelectionAndParentLink)
	{
		auto window = WindowWithAtoms();
		auto path = Line();
		for (auto &node : path.nodes) node.binding.value = PathBinding::CopyPosition{1, {}, 0};
		const auto id = SceneSystem::AppendScenePath(window, path);
		window.selectedScenePaths = {id};
		window.defectFrameChildren.paths = {id};
		RemapSceneAtomReferencesAfterAtomRemoval(window, {1}, {0});
		EXPECT_FALSE(window.paths->Store().Contains(id));
		EXPECT_TRUE(window.selectedScenePaths.empty());
		EXPECT_TRUE(window.defectFrameChildren.paths.empty());
	}
}
