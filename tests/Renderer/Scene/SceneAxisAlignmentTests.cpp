#include <gtest/gtest.h>
#include <limits>
#include "Renderer/Scene/SceneAxisAlignment.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneAxisAlignmentTests, EveryMappingPreservesARightHandedFrameAndTheSecondAxis)
	{
		const auto current = glm::mat3_cast(glm::quat(glm::radians(glm::vec3(17, -38, 61))));
		const auto defect = glm::mat3_cast(glm::quat(glm::radians(glm::vec3(-24, 43, 12))));
		for (int own = 0; own < 3; ++own)
			for (int target = 0; target < 3; ++target)
			{
				const auto rotation = SceneAxisAlignmentRotation(current, defect, own, target);
				ASSERT_TRUE(rotation.has_value());
				const glm::mat3 aligned = glm::mat3_cast(*rotation) * current;
				EXPECT_NEAR(glm::length(aligned[own] - defect[target]), 0.0f, 1e-4f);
				EXPECT_NEAR(glm::length(aligned[own == 0 ? 1 : 0] - defect[target == 0 ? 1 : 0]), 0.0f, 1e-4f);
				EXPECT_NEAR(glm::determinant(aligned), 1.0f, 1e-4f);
				EXPECT_NEAR(glm::dot(aligned[0], aligned[1]), 0.0f, 1e-4f);
			}
	}

	TEST(SceneAxisAlignmentTests, RejectsInvalidAxesNonFiniteAndDegenerateFrames)
	{
		const glm::mat3 identity(1.0f);
		EXPECT_FALSE(SceneAxisAlignmentRotation(identity, identity, -1, 0));
		EXPECT_FALSE(SceneAxisAlignmentRotation(identity, identity, 1, 3));
		EXPECT_FALSE(SceneAxisAlignmentRotation(glm::mat3(0.0f), identity, 1, 2));
		glm::mat3 invalid(1.0f);
		invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(SceneAxisAlignmentRotation(invalid, identity, 1, 2));
		EXPECT_FALSE(SceneAxisAlignmentRotation(identity, invalid, 1, 2));
		invalid = identity;
		invalid[0] = -invalid[0];
		EXPECT_FALSE(SceneAxisAlignmentRotation(identity, invalid, 1, 2));
	}

	TEST(SceneAxisAlignmentTests, PlaneYAlongDefectZKeepsItsCentreAndSurvivesAnchorRefresh)
	{
		RendererWindowState window;
		window.structure.defectFrame = DefectFrame{glm::vec3(30.0f), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)};
		for (const auto point : {glm::vec3(1, 2, 3), glm::vec3(2, 2, 3)})
		{
			RendererAtomData atom;
			atom.cartesianPosition = point;
			window.structure.atoms.push_back(atom);
		}
		RendererWindowState::ScenePlane plane;
		plane.id = SceneObjectId{1};
		plane.center = glm::vec3(1.5f, 2, 3);
		plane.anchorAtoms = {0, 1};
		window.scenePlanes.push_back(plane);
		window.selectedScenePlanes = {plane.id};
		ASSERT_EQ(AlignSelectedSceneObjectAxes(window, 1, 2), 1u);
		ResolveAnchoredScenePlanes(window);
		const auto &aligned = window.scenePlanes.front();
		EXPECT_EQ(aligned.center, plane.center);
		EXPECT_EQ(aligned.halfExtents, plane.halfExtents);
		EXPECT_TRUE(aligned.anchorAtoms.empty());
		EXPECT_NEAR(glm::length(glm::cross(aligned.normal, aligned.tangent) - glm::vec3(0, 0, 1)), 0.0f, 1e-4f);
		EXPECT_NEAR(glm::length(aligned.tangent - glm::vec3(1, 0, 0)), 0.0f, 1e-4f);
	}

	TEST(SceneAxisAlignmentTests, OrbitalAlignmentKeepsAnchorsAndIgnoresUnselectedAndTwoCentreObjects)
	{
		RendererWindowState window;
		window.structure.defectFrame = DefectFrame{};
		RendererAtomData atom;
		atom.cartesianPosition = glm::vec3(5, 2, 1);
		window.structure.atoms.push_back(atom);
		RendererWindowState::SceneOrbital orbital;
		orbital.id = SceneObjectId{1};
		orbital.preset = OrbitalPreset::P;
		orbital.anchorAtoms = {0};
		window.sceneOrbitals.push_back(orbital);
		orbital.id = SceneObjectId{2};
		window.sceneOrbitals.push_back(orbital);
		orbital.id = SceneObjectId{3};
		orbital.preset = OrbitalPreset::Sigma;
		window.sceneOrbitals.push_back(orbital);
		window.selectedSceneOrbitals = {SceneObjectId{1}, SceneObjectId{3}, SceneObjectId{999}};
		ASSERT_EQ(AlignSelectedSceneObjectAxes(window, 1, 2), 1u);
		const auto basis = glm::mat3_cast(glm::quat(glm::radians(window.sceneOrbitals[0].rotationEuler)));
		EXPECT_NEAR(glm::length(basis[1] - glm::vec3(0, 0, 1)), 0.0f, 1e-4f);
		EXPECT_EQ(window.sceneOrbitals[0].anchorAtoms, std::vector<std::size_t>{0});
		EXPECT_EQ(window.sceneOrbitals[1].rotationEuler, glm::vec3(0.0f));
		EXPECT_EQ(window.sceneOrbitals[2].rotationEuler, glm::vec3(0.0f));
		window.structure.defectFrame.reset();
		EXPECT_EQ(AlignSelectedSceneObjectAxes(window, 1, 2), 0u);
	}

	TEST(SceneAxisAlignmentTests, PathRotationComposesAboutItsOwnOrigin)
	{
		RendererWindowState window;
		window.structure.defectFrame = DefectFrame{};
		ScenePath path;
		path.id = SceneObjectId{1};
		path.transform.position = glm::vec3(4, 5, 6);
		path.transform.rotation = glm::quat(glm::radians(glm::vec3(30, -20, 40)));
		PathNode first;
		first.id = AllocateElementId(path);
		PathNode second;
		second.id = AllocateElementId(path);
		second.position = glm::vec3(0, 0, 2);
		path.nodes = {first, second};
		PathSegment segment;
		segment.id = AllocateElementId(path);
		segment.data = LineSegmentData{};
		path.segments = {segment};
		ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(path));
		window.selectedScenePaths = {path.id};
		ASSERT_EQ(AlignSelectedSceneObjectAxes(window, 1, 2), 1u);
		window.paths->Store().Visit([&](const ScenePath &aligned) {
			EXPECT_EQ(aligned.transform.position, path.transform.position);
			EXPECT_EQ(aligned.nodes[1].position, path.nodes[1].position);
			EXPECT_NEAR(glm::length(glm::mat3_cast(aligned.transform.rotation)[1] - glm::vec3(0, 0, 1)), 0.0f, 1e-4f);
		});
	}
}
