#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <glm/gtc/quaternion.hpp>
#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneAxisAlignment.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/ScenePlanePlacement.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		DefectFrame RotatedFrame()
		{
			const auto axes = glm::mat3_cast(glm::quat(glm::radians(glm::vec3(23, -37, 61))));
			return {glm::vec3(4, -2, 7), axes[0], axes[1], axes[2]};
		}
		void ExpectVector(const glm::vec3 &actual, const glm::vec3 &expected)
		{
			EXPECT_NEAR(glm::length(actual - expected), 0.0f, 2e-4f);
		}
		ScenePath FreePath(SceneObjectId id)
		{
			ScenePath path;
			path.id = id;
			path.transform.position = glm::vec3(8, 3, -2);
			path.transform.rotation = glm::quat(glm::radians(glm::vec3(10, 20, 30)));
			path.transform.scale = glm::vec3(2, 3, 4);
			PathNode a, b;
			a.id = AllocateElementId(path);
			b.id = AllocateElementId(path);
			b.position = glm::vec3(0, 0, 2);
			path.nodes = {a, b};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}
	}

	TEST(ScenePlanePlacementTests, PresetsUseRotatedDefectAxesAndOriginKeepingStyleAndSize)
	{
		const auto frame = RotatedFrame();
		const std::array normals = {frame.z, frame.y, frame.x};
		const std::array tangents = {frame.x, frame.x, frame.y};
		for (int index = 0; index < 3; ++index)
		{
			RendererWindowState window;
			window.structure.defectFrame = frame;
			auto plane = MakeDefaultScenePlane(window, glm::vec3(9, 8, 7));
			const auto extent = plane.halfExtents;
			plane.id = SceneObjectId{21};
			plane.anchorAtoms = {0, 1};
			plane.color = glm::vec3(0.2f, 0.3f, 0.4f);
			plane.alpha = 0.42f;
			ASSERT_TRUE(SetScenePlaneInDefectFrame(plane, frame, static_cast<ScenePlaneDefectPreset>(index)));
			ExpectVector(plane.center, frame.origin);
			ExpectVector(plane.normal, normals[index]);
			ExpectVector(plane.tangent, tangents[index]);
			EXPECT_NEAR(glm::dot(plane.normal, plane.tangent), 0.0f, 1e-5f);
			EXPECT_EQ(plane.halfExtents, extent);
			EXPECT_EQ(plane.id, SceneObjectId{21});
			EXPECT_EQ(plane.color, glm::vec3(0.2f, 0.3f, 0.4f));
			EXPECT_FLOAT_EQ(plane.alpha, 0.42f);
			EXPECT_TRUE(plane.anchorAtoms.empty());
		}
	}

	TEST(ScenePlanePlacementTests, MultiSelectionDetachesOnlySelectedPlanesAndOneUndoRestoresAll)
	{
		RendererWindowState window;
		window.windowId = "plane-placement";
		window.structure.defectFrame = RotatedFrame();
		for (int index = 0; index < 3; ++index)
		{
			RendererWindowState::ScenePlane plane;
			plane.id = SceneObjectId{static_cast<std::uint64_t>(index + 1)};
			plane.center = glm::vec3(static_cast<float>(index), 2, 3);
			plane.anchorAtoms = {0, 1};
			window.scenePlanes.push_back(plane);
		}
		window.selectedScenePlanes = {SceneObjectId{1}, SceneObjectId{3}, SceneObjectId{999}};
		const auto original = window.scenePlanes;
		auto before = CaptureSceneObjectsSnapshot(window);
		ASSERT_EQ(PlaceSelectedScenePlanesInDefectFrame(window, ScenePlaneDefectPreset::YZ), 2u);
		ResolveAnchoredScenePlanes(window);
		ExpectVector(window.scenePlanes[0].center, window.structure.defectFrame->origin);
		ExpectVector(window.scenePlanes[2].center, window.structure.defectFrame->origin);
		EXPECT_EQ(window.scenePlanes[1].center, original[1].center);
		EXPECT_EQ(window.scenePlanes[1].anchorAtoms, original[1].anchorAtoms);
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			[&window](const std::string &) { return &window; }, window.windowId, std::move(before))));
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
		ASSERT_TRUE(stack.Undo());
		EXPECT_EQ(stack.GetUndoDepth(), 0u);
		for (std::size_t index = 0; index < original.size(); ++index)
		{
			EXPECT_EQ(window.scenePlanes[index].center, original[index].center);
			EXPECT_EQ(window.scenePlanes[index].normal, original[index].normal);
			EXPECT_EQ(window.scenePlanes[index].tangent, original[index].tangent);
			EXPECT_EQ(window.scenePlanes[index].anchorAtoms, original[index].anchorAtoms);
		}
		ASSERT_TRUE(stack.Redo());
		ExpectVector(window.scenePlanes[0].normal, window.structure.defectFrame->x);
		ExpectVector(window.scenePlanes[2].center, window.structure.defectFrame->origin);
		EXPECT_TRUE(window.scenePlanes[0].anchorAtoms.empty());
	}

	TEST(ScenePlanePlacementTests, MissingAndInvalidFramesAndPresetsLeavePlanesUntouched)
	{
		RendererWindowState window;
		EXPECT_EQ(PlaceSelectedScenePlanesInDefectFrame(window, ScenePlaneDefectPreset::XY), 0u);
		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(5);
		plane.anchorAtoms = {4, 5};
		auto frame = RotatedFrame();
		EXPECT_FALSE(SetScenePlaneInDefectFrame(plane, frame, static_cast<ScenePlaneDefectPreset>(9)));
		frame.origin.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(SetScenePlaneInDefectFrame(plane, frame, ScenePlaneDefectPreset::XY));
		frame = RotatedFrame();
		frame.x = glm::vec3(0);
		EXPECT_FALSE(SetScenePlaneInDefectFrame(plane, frame, ScenePlaneDefectPreset::XZ));
		EXPECT_EQ(plane.center, glm::vec3(5));
		EXPECT_EQ(plane.anchorAtoms, (std::vector<std::size_t>{4, 5}));
	}

	TEST(ScenePlanePlacementTests, EulerFrameRoundTripIncludesWrappedAnglesAndNearGimbalLock)
	{
		for (const auto degrees : {glm::vec3(0), glm::vec3(23, -37, 61), glm::vec3(-170, 89.9f, 178),
			glm::vec3(130, -89.9f, -120), glm::vec3(385, 137, -412)})
		{
			RendererWindowState::ScenePlane plane;
			plane.center = glm::vec3(5);
			plane.halfExtents = glm::vec2(2, 3);
			ASSERT_TRUE(SetScenePlaneEulerDegrees(plane, degrees));
			const auto expected = glm::mat3_cast(glm::quat(glm::radians(degrees)));
			ExpectVector(plane.tangent, expected[0]);
			ExpectVector(plane.normal, expected[2]);
			const auto normal = plane.normal;
			const auto tangent = plane.tangent;
			const auto recovered = ScenePlaneEulerDegrees(plane);
			ASSERT_TRUE(recovered);
			ASSERT_TRUE(SetScenePlaneEulerDegrees(plane, *recovered));
			ExpectVector(plane.normal, normal);
			ExpectVector(plane.tangent, tangent);
			EXPECT_EQ(plane.center, glm::vec3(5));
			EXPECT_EQ(plane.halfExtents, glm::vec2(2, 3));
		}
		// Round-trip also starts with each preset's normal/tangent rather than Euler angles.
		for (const auto preset : {ScenePlaneDefectPreset::XY, ScenePlaneDefectPreset::XZ, ScenePlaneDefectPreset::YZ})
		{
			RendererWindowState::ScenePlane plane;
			ASSERT_TRUE(SetScenePlaneInDefectFrame(plane, RotatedFrame(), preset));
			const auto normal = plane.normal;
			const auto tangent = plane.tangent;
			const auto recovered = ScenePlaneEulerDegrees(plane);
			ASSERT_TRUE(recovered);
			ASSERT_TRUE(SetScenePlaneEulerDegrees(plane, *recovered));
			ExpectVector(plane.normal, normal);
			ExpectVector(plane.tangent, tangent);
		}
	}

	TEST(ScenePlanePlacementTests, EulerRejectsInvalidInputAndAnchoredPlane)
	{
		RendererWindowState::ScenePlane plane;
		EXPECT_FALSE(SetScenePlaneEulerDegrees(plane, glm::vec3(std::numeric_limits<float>::infinity())));
		EXPECT_EQ(plane.normal, glm::vec3(0, 0, 1));
		plane.anchorAtoms = {0, 1};
		EXPECT_FALSE(SetScenePlaneEulerDegrees(plane, glm::vec3(25)));
		EXPECT_EQ(plane.tangent, glm::vec3(1, 0, 0));
		plane.normal = plane.tangent;
		EXPECT_FALSE(ScenePlaneEulerDegrees(plane));
	}

	TEST(ScenePlanePlacementTests, OrderingIsBackToFrontStableAndChangesWithTheView)
	{
		std::vector<RendererWindowState::ScenePlane> planes(5);
		planes[0].center = glm::vec3(0, 0, 1);
		planes[1].center = glm::vec3(0, 0, -4);
		planes[2].center = glm::vec3(0, 0, 1);
		planes[3].center = glm::vec3(0, 0, 50);
		planes[3].visible = false;
		planes[4].center.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_EQ(ScenePlaneBackToFrontOrder(planes, glm::vec3(0, 0, 10)), (std::vector<std::size_t>{1, 0, 2}));
		EXPECT_EQ(ScenePlaneBackToFrontOrder(planes, glm::vec3(0, 0, -10)), (std::vector<std::size_t>{0, 2, 1}));
		planes[0].center = glm::vec3(20, 0, 0);
		EXPECT_EQ(ScenePlaneBackToFrontOrder(planes, glm::vec3(0)), (std::vector<std::size_t>{0, 1, 2}));
		EXPECT_TRUE(ScenePlaneBackToFrontOrder({}, glm::vec3(0)).empty());
		EXPECT_TRUE(ScenePlaneBackToFrontOrder(planes, glm::vec3(std::numeric_limits<float>::infinity())).empty());
	}

	TEST(ScenePlanePlacementTests, CentreOnlyMovePreservesPlaneFrameAndIgnoresUnselectedChildren)
	{
		RendererWindowState window;
		window.structure.defectFrame = RotatedFrame();
		RendererWindowState::ScenePlane plane;
		plane.id = SceneObjectId{1};
		ASSERT_TRUE(SetScenePlaneEulerDegrees(plane, glm::vec3(13, 29, 43)));
		plane.anchorAtoms = {0, 1};
		window.scenePlanes = {plane};
		plane.id = SceneObjectId{2};
		window.scenePlanes.push_back(plane);
		window.selectedScenePlanes = {SceneObjectId{1}};
		window.defectFrameSelected = true;
		window.defectFrameChildren.planes = {SceneObjectId{2}};
		ASSERT_EQ(MoveSelectedSceneObjectOriginsToDefect(window), 1u);
		ExpectVector(window.scenePlanes[0].center, window.structure.defectFrame->origin);
		ExpectVector(window.scenePlanes[0].normal, plane.normal);
		ExpectVector(window.scenePlanes[0].tangent, plane.tangent);
		EXPECT_TRUE(window.scenePlanes[0].anchorAtoms.empty());
		EXPECT_EQ(window.scenePlanes[1].center, plane.center);
		EXPECT_EQ(window.scenePlanes[1].anchorAtoms, plane.anchorAtoms);
	}

	TEST(ScenePlanePlacementTests, CentreOnlyMovePreservesOrbitalAxisAndMovesTwoCentresTogether)
	{
		RendererWindowState window;
		window.structure.defectFrame = RotatedFrame();
		RendererWindowState::SceneOrbital orbital;
		orbital.id = SceneObjectId{1};
		orbital.preset = OrbitalPreset::Sigma;
		orbital.centerA = glm::vec3(1, 2, 3);
		orbital.centerB = glm::vec3(4, 6, 8);
		orbital.rotationEuler = glm::vec3(17, 29, 38);
		window.sceneOrbitals = {orbital};
		window.selectedSceneOrbitals = {orbital.id};
		ASSERT_EQ(MoveSelectedSceneObjectOriginsToDefect(window), 1u);
		ExpectVector((window.sceneOrbitals[0].centerA + window.sceneOrbitals[0].centerB) * 0.5f, window.structure.defectFrame->origin);
		ExpectVector(window.sceneOrbitals[0].centerB - window.sceneOrbitals[0].centerA, orbital.centerB - orbital.centerA);
		EXPECT_EQ(window.sceneOrbitals[0].rotationEuler, orbital.rotationEuler);
	}

	TEST(ScenePlanePlacementTests, CentreOnlyMoveHandlesFreePathsAndLabelAnchorOffsetsWithBatchUndo)
	{
		RendererWindowState window;
		window.windowId = "origin-move";
		window.structure.defectFrame = RotatedFrame();
		const auto path = FreePath(SceneObjectId{3});
		ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(path));
		window.selectedScenePaths = {path.id};
		RendererAtomData atom;
		atom.cartesianPosition = glm::vec3(9, 8, 7);
		window.structure.atoms.push_back(atom);
		RendererWindowState::FreeLabel label;
		label.id = SceneObjectId{4};
		label.worldPosition = atom.cartesianPosition;
		label.anchorAtom = 0;
		window.freeLabels.push_back(label);
		window.selectedFreeLabels = {label.id};
		auto before = CaptureSceneObjectsSnapshot(window);
		ASSERT_EQ(MoveSelectedSceneObjectOriginsToDefect(window), 2u);
		RefreshAnchoredFreeLabels(window);
		ExpectVector(window.freeLabels[0].worldPosition, window.structure.defectFrame->origin);
		EXPECT_EQ(window.freeLabels[0].anchorAtom, label.anchorAtom);
		window.paths->Store().Visit([&](const ScenePath &moved) {
			ExpectVector(moved.transform.position, window.structure.defectFrame->origin);
			EXPECT_EQ(moved.transform.rotation, path.transform.rotation);
			EXPECT_EQ(moved.transform.scale, path.transform.scale);
			EXPECT_EQ(moved.nodes[1].position, path.nodes[1].position);
		});
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			[&window](const std::string &) { return &window; }, window.windowId, std::move(before))));
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
		ASSERT_TRUE(stack.Undo());
		EXPECT_EQ(window.freeLabels[0].worldPosition, label.worldPosition);
		window.paths->Store().Visit([&](const ScenePath &restored) { EXPECT_EQ(restored.transform.position, path.transform.position); });
		ASSERT_TRUE(stack.Redo());
		ExpectVector(window.freeLabels[0].worldPosition, window.structure.defectFrame->origin);
	}

	TEST(ScenePlanePlacementTests, BondFramePathsHaveNoIndependentlyMovableOrigin)
	{
		RendererWindowState window;
		window.structure.defectFrame = RotatedFrame();
		auto path = FreePath(SceneObjectId{1});
		path.transformBinding.value = PathTransformBinding::BondFrame{0, 1, 0};
		ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(path));
		window.selectedScenePaths = {path.id};
		EXPECT_TRUE(CollectSceneObjectOriginTargets(window).empty());
		EXPECT_EQ(MoveSelectedSceneObjectOriginsToDefect(window), 0u);
	}
}
