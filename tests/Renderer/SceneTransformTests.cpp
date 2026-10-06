#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Core/Commands/Command.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-3f;

		void ExpectVec3Near(const glm::vec3 &actual, const glm::vec3 &expected)
		{
			EXPECT_NEAR(actual.x, expected.x, kEpsilon);
			EXPECT_NEAR(actual.y, expected.y, kEpsilon);
			EXPECT_NEAR(actual.z, expected.z, kEpsilon);
		}

		class AtomPositionSnapshotCommand final : public ICommand
		{
		public:
			AtomPositionSnapshotCommand(RendererWindowState &window, glm::vec3 before)
				: m_Window(window), m_Before(before)
			{
			}

			Result<void> Execute(CommandContext &) override { return {}; }
			Result<void> Undo(CommandContext &) override
			{
				m_After = m_Window.structure.atoms[0].cartesianPosition;
				m_Window.structure.atoms[0].cartesianPosition = m_Before;
				return {};
			}
			Result<void> Redo(CommandContext &) override
			{
				m_Window.structure.atoms[0].cartesianPosition = *m_After;
				return {};
			}
			[[nodiscard]] std::string Description() const override { return "Move atom"; }
			[[nodiscard]] bool IsUndoable() const noexcept override { return true; }

		private:
			RendererWindowState &m_Window;
			glm::vec3 m_Before;
			std::optional<glm::vec3> m_After;
		};
	} // namespace

	TEST(SceneTransformTests, RestoringSelectionCancelsLabelTransform)
	{
		RendererWindowState window;
		RendererWindowState::FreeLabel label;
		label.id = SceneObjectId{2};
		label.worldPosition = glm::vec3(1.0f, 2.0f, 3.0f);
		label.rotationRadians = 0.25f;
		label.style.scale = 1.5f;
		window.freeLabels.push_back(label);
		window.selectedFreeLabels = {label.id};
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(4.0f, 0.0f, 0.0f);

		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate, TransformPivotMode::Median, label.worldPosition);
		ASSERT_GT(glm::distance(window.freeLabels[0].worldPosition, label.worldPosition), kEpsilon);
		RestoreSceneTransformSelection(window, snapshot);

		ExpectVec3Near(window.freeLabels[0].worldPosition, label.worldPosition);
		EXPECT_FLOAT_EQ(window.freeLabels[0].rotationRadians, label.rotationRadians);
		EXPECT_FLOAT_EQ(window.freeLabels[0].style.scale, label.style.scale);
	}

	TEST(SceneTransformTests, MixedAtomAndPathConfirmCreatesOneUndoEntryAndRestoresBoth)
	{
		RendererWindowState window;
		window.windowId = "mixed";
		window.structure.atoms.emplace_back();
		window.structure.atoms[0].cartesianPosition = glm::vec3(0.0f);
		window.selectedAtomIndices = {0};
		ScenePath path;
		path.transform.position = glm::vec3(0.0f, 1.0f, 0.0f);
		path.nodes = {{AllocateElementId(path), glm::vec3(0.0f), {}},
			{AllocateElementId(path), glm::vec3(0.0f, 1.0f, 0.0f), {}}};
		path.segments = {{AllocateElementId(path), LineSegmentData{}}};
		ASSERT_TRUE(ValidatePath(path).empty());
		const SceneObjectId pathId = SceneSystem::AppendScenePath(window, path);
		window.selectedScenePaths = {pathId};
		const glm::vec3 atomBefore = window.structure.atoms[0].cartesianPosition;
		const SceneObjectsSnapshot sceneBefore = CaptureSceneObjectsSnapshot(window);
		SceneTransformSelectionSnapshot transformBefore = CaptureSceneTransformSelection(window);
		// The application records atom and scene changes in one group when both are committed.
		transformBefore.atoms = {{0, atomBefore}};
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(2.0f, 0.0f, 0.0f);
		ApplySceneTransformSelection(
			window, transformBefore, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));
		ASSERT_EQ(window.structure.atoms[0].cartesianPosition, atomBefore + delta.spatial.translation);
		ASSERT_EQ(window.paths->Store().Find(pathId)->transform.position, path.transform.position + delta.spatial.translation);

		UndoStack stack;
		auto group = stack.ScopedGroup("Move selection");
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			[&window](const std::string &) { return &window; }, window.windowId, sceneBefore)));
		ASSERT_TRUE(stack.PushExecuted(CreateUnique<AtomPositionSnapshotCommand>(window, atomBefore)));
		ASSERT_TRUE(group.Commit().HasValue());
		ASSERT_EQ(stack.GetUndoDepth(), 1u);

		ASSERT_TRUE(stack.Undo().HasValue());
		ExpectVec3Near(window.structure.atoms[0].cartesianPosition, atomBefore);
		ExpectVec3Near(window.paths->Store().Find(pathId)->transform.position, path.transform.position);
		ASSERT_TRUE(stack.Redo().HasValue());
		ExpectVec3Near(window.structure.atoms[0].cartesianPosition, atomBefore + delta.spatial.translation);
		ExpectVec3Near(window.paths->Store().Find(pathId)->transform.position, path.transform.position + delta.spatial.translation);
	}
	// --- Task 31 #14: the gizmo must move orbitals and planes, not only atoms, labels and paths.

	namespace
	{
		void ExpectPlaneFrameOrthonormal(const RendererWindowState::ScenePlane &plane)
		{
			EXPECT_NEAR(glm::length(plane.normal), 1.0f, kEpsilon);
			EXPECT_NEAR(glm::length(plane.tangent), 1.0f, kEpsilon);
			EXPECT_NEAR(glm::dot(plane.normal, plane.tangent), 0.0f, kEpsilon);
		}

		RendererWindowState MakeOrbitalAndPlaneWindow()
		{
			RendererWindowState window;
			window.windowId = "w";

			RendererWindowState::SceneOrbital orbital;
			orbital.id = SceneObjectId{101};
			// A two-centre preset on purpose: centerB is only a real centre for these, and the
			// cases below are about both centres moving together.
			orbital.preset = OrbitalPreset::Sigma;
			orbital.centerA = glm::vec3(1.0f, 0.0f, 0.0f);
			orbital.centerB = glm::vec3(3.0f, 0.0f, 0.0f);
			orbital.scale = 1.0f;
			window.sceneOrbitals.push_back(orbital);
			window.selectedSceneOrbitals = {orbital.id};

			RendererWindowState::ScenePlane plane;
			plane.id = SceneObjectId{202};
			plane.center = glm::vec3(0.0f, 2.0f, 0.0f);
			plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
			plane.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
			plane.halfExtents = glm::vec2(2.0f, 1.0f);
			window.scenePlanes.push_back(plane);
			window.selectedScenePlanes = {plane.id};
			return window;
		}
	} // namespace

	TEST(SceneObjectGizmoTests, TheSnapshotPicksUpSelectedOrbitalsAndPlanes)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();

		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		ASSERT_EQ(snapshot.orbitals.size(), 1u);
		ASSERT_EQ(snapshot.planes.size(), 1u);
		ExpectVec3Near(snapshot.orbitals[0].centerA, glm::vec3(1.0f, 0.0f, 0.0f));
		ExpectVec3Near(snapshot.planes[0].center, glm::vec3(0.0f, 2.0f, 0.0f));
		EXPECT_TRUE(HasSceneObjectTransformTargets(snapshot));
		EXPECT_FALSE(SceneTransformPivotPositions(snapshot).empty());
	}

	TEST(SceneObjectGizmoTests, TranslateMovesBothCentresAndThePlane)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(0.0f, 0.0f, 5.0f);
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));

		ExpectVec3Near(window.sceneOrbitals[0].centerA, glm::vec3(1.0f, 0.0f, 5.0f));
		ExpectVec3Near(window.sceneOrbitals[0].centerB, glm::vec3(3.0f, 0.0f, 5.0f));
		ExpectVec3Near(window.scenePlanes[0].center, glm::vec3(0.0f, 2.0f, 5.0f));
	}

	TEST(SceneObjectGizmoTests, TranslatingAnAnchoredOrbitalDropsTheAnchor)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		window.structure.atoms.push_back(RendererAtomData{"C", glm::vec3(1.0f, 0.0f, 0.0f)});
		window.sceneOrbitals[0].anchorAtoms = {0};
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.orbitals.size(), 1u);
		EXPECT_TRUE(snapshot.orbitals[0].anchored);

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(0.0f, 1.0f, 0.0f);
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));

		// An anchor resolved every frame would drag the orbital straight back onto its atom.
		EXPECT_TRUE(window.sceneOrbitals[0].anchorAtoms.empty());
		ExpectVec3Near(window.sceneOrbitals[0].centerA, glm::vec3(1.0f, 1.0f, 0.0f));
	}

	TEST(SceneObjectGizmoTests, ScaleGrowsTheOrbitalAndThePlaneExtents)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		delta.scaleFactor = 2.0f;
		delta.spatial.linear = ConstrainedScaleMatrix(
			delta.scaleFactor, TransformConstraint{}, TransformBases{});
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Scale, TransformPivotMode::Median, glm::vec3(0.0f));

		EXPECT_NEAR(window.sceneOrbitals[0].scale, 2.0f, kEpsilon);
		EXPECT_NEAR(window.scenePlanes[0].halfExtents.x, 4.0f, kEpsilon);
		EXPECT_NEAR(window.scenePlanes[0].halfExtents.y, 2.0f, kEpsilon);
		ExpectPlaneFrameOrthonormal(window.scenePlanes[0]);
	}

	TEST(SceneObjectGizmoTests, ScaleAlongPlaneTangentChangesOnlyItsWidth)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		window.scenePlanes[0].tangent = glm::vec3(0.0f, 1.0f, 0.0f);
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		delta.scaleFactor = 2.0f;
		delta.spatial.linear = ConstrainedScaleMatrix(
			delta.scaleFactor,
			TransformConstraint{ConstraintKind::Axis, 1, TransformOrientation::Global}, TransformBases{});
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Scale, TransformPivotMode::Median, glm::vec3(0.0f));

		EXPECT_NEAR(window.scenePlanes[0].halfExtents.x, 4.0f, kEpsilon);
		EXPECT_NEAR(window.scenePlanes[0].halfExtents.y, 1.0f, kEpsilon);
		ExpectPlaneFrameOrthonormal(window.scenePlanes[0]);
	}

	TEST(SceneObjectGizmoTests, ScaleAlongPlaneBitangentChangesOnlyItsHeight)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		window.scenePlanes[0].tangent = glm::vec3(0.0f, 1.0f, 0.0f);
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		delta.scaleFactor = 2.0f;
		delta.spatial.linear = ConstrainedScaleMatrix(
			delta.scaleFactor,
			TransformConstraint{ConstraintKind::Axis, 0, TransformOrientation::Global}, TransformBases{});
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Scale, TransformPivotMode::Median, glm::vec3(0.0f));

		EXPECT_NEAR(window.scenePlanes[0].halfExtents.x, 2.0f, kEpsilon);
		EXPECT_NEAR(window.scenePlanes[0].halfExtents.y, 2.0f, kEpsilon);
		ExpectPlaneFrameOrthonormal(window.scenePlanes[0]);
	}

	TEST(SceneObjectGizmoTests, RotateCarriesThePlaneFrameRoundAndKeepsItOrthonormal)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		// A quarter turn about +X: the plane's +Z normal should end up along -Y (right-handed).
		delta.spatial.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
		delta.rotationRadians = glm::half_pi<float>();
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Rotate, TransformPivotMode::Median, glm::vec3(0.0f));

		const RendererWindowState::ScenePlane &plane = window.scenePlanes[0];
		ExpectVec3Near(plane.normal, glm::vec3(0.0f, -1.0f, 0.0f));
		ExpectPlaneFrameOrthonormal(plane);
	}

	TEST(SceneObjectGizmoTests, RestorePutsOrbitalsAndPlanesBack)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(7.0f, 7.0f, 7.0f);
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));
		RestoreSceneTransformSelection(window, snapshot);

		ExpectVec3Near(window.sceneOrbitals[0].centerA, glm::vec3(1.0f, 0.0f, 0.0f));
		ExpectVec3Near(window.scenePlanes[0].center, glm::vec3(0.0f, 2.0f, 0.0f));
	}

	TEST(SceneObjectGizmoTests, ASingleCentreOrbitalPivotsOnItsOwnCentreOnly)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		window.selectedScenePlanes.clear();
		// A p orbital never reads centerB, but it still carries the default (1.5, 0, 0). Counting
		// that as a centre put the gizmo halfway between the orbital and a point nothing is at.
		window.sceneOrbitals[0].preset = OrbitalPreset::P;
		window.sceneOrbitals[0].centerB = glm::vec3(3.0f, 0.0f, 0.0f);

		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		const std::vector<glm::vec3> positions = SceneTransformPivotPositions(snapshot);

		ASSERT_EQ(positions.size(), 1u);
		ExpectVec3Near(positions[0], glm::vec3(1.0f, 0.0f, 0.0f));
	}

	TEST(SceneObjectGizmoTests, AtomsAreNotGizmoTargetsWhileASceneObjectIsSelected)
	{
		RendererWindowState window = MakeOrbitalAndPlaneWindow();
		RendererAtomData atom;
		atom.cartesianPosition = glm::vec3(5.0f, 5.0f, 5.0f);
		window.structure.atoms.push_back(atom);
		// Fitting a plane to atoms leaves them selected; dragging the plane used to drag them too.
		window.selectedAtomIndices = {0};

		EXPECT_TRUE(CaptureSceneTransformSelection(window).atoms.empty());

		window.selectedSceneOrbitals.clear();
		window.selectedScenePlanes.clear();
		EXPECT_EQ(CaptureSceneTransformSelection(window).atoms.size(), 1u);
	}
} // namespace DefectStudio::Tests
