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
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

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

		RendererWindowState::SceneArrow MakeArrow(
			SceneObjectId id, const glm::vec3 &start, const glm::vec3 &end)
		{
			RendererWindowState::SceneArrow arrow;
			arrow.id = id;
			arrow.start = start;
			arrow.end = end;
			return arrow;
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

	TEST(SceneTransformTests, ArrowTranslateAlongLatticeAxisMovesBothEndpoints)
	{
		RendererWindowState window;
		window.sceneArrows.push_back(MakeArrow(SceneObjectId{1}, {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}));
		window.selectedSceneArrows = {SceneObjectId{1}};
		const SceneTransformSelectionSnapshot snapshot =
			CaptureSceneTransformSelection(window, SceneArrowTransformTarget::Both);

		TransformBases bases;
		bases.lattice = glm::mat3(
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(-0.5f, std::sqrt(3.0f) * 0.5f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f));
		ModalTransformSession session = BeginModalTransform(
			ModalTransformOp::Translate, TransformOrientation::Lattice, bases,
			glm::vec3(0.0f), glm::vec2(0.0f));
		session.constraint = TransformConstraint{
			ConstraintKind::Axis, 1, TransformOrientation::Lattice};
		session.numericText = "2";
		SceneTransformDelta delta;
		delta.spatial = EvaluateModalTransform(
			session, ModalTransformView{}, glm::vec2(0.0f), SnapMode::Off, TransformSnapSteps{});
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));

		ExpectVec3Near(window.sceneArrows[0].start, snapshot.arrows[0].start + delta.spatial.translation);
		ExpectVec3Near(window.sceneArrows[0].end, snapshot.arrows[0].end + delta.spatial.translation);
	}

	TEST(SceneTransformTests, SelectedArrowTipTranslatesAloneAndCancelRestoresIt)
	{
		RendererWindowState window;
		window.sceneArrows.push_back(MakeArrow(SceneObjectId{4}, {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}));
		window.selectedSceneArrows = {SceneObjectId{4}};
		window.sceneArrowGizmoActiveArrowIndex = 0;
		window.sceneArrowGizmoActiveTarget = RendererWindowState::SceneArrowDragTarget::End;
		const SceneTransformSelectionSnapshot snapshot =
			CaptureSceneTransformSelectionForOperation(window, ModalTransformOp::Translate);
		const std::vector<glm::vec3> pivotPositions = SceneTransformPivotPositions(snapshot);
		ASSERT_EQ(pivotPositions.size(), 1u);
		ExpectVec3Near(pivotPositions.front(), glm::vec3(4.0f, 5.0f, 6.0f));

		TransformBases bases;
		ModalTransformSession session = BeginModalTransform(
			ModalTransformOp::Translate, TransformOrientation::Global, bases,
			pivotPositions.front(), glm::vec2(0.0f));
		session.constraint = TransformConstraint{
			ConstraintKind::Axis, 0, TransformOrientation::Global};
		session.numericText = "2";
		SceneTransformDelta delta;
		delta.spatial = EvaluateModalTransform(
			session, ModalTransformView{}, glm::vec2(0.0f), SnapMode::Off, TransformSnapSteps{});
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, pivotPositions.front());

		ExpectVec3Near(window.sceneArrows[0].start, glm::vec3(1.0f, 2.0f, 3.0f));
		ExpectVec3Near(window.sceneArrows[0].end, glm::vec3(6.0f, 5.0f, 6.0f));
		RestoreSceneTransformSelection(window, snapshot);
		ExpectVec3Near(window.sceneArrows[0].start, glm::vec3(1.0f, 2.0f, 3.0f));
		ExpectVec3Near(window.sceneArrows[0].end, glm::vec3(4.0f, 5.0f, 6.0f));
	}

	TEST(SceneTransformTests, ArrowRotateUsesMedianPivot)
	{
		RendererWindowState window;
		window.sceneArrows.push_back(MakeArrow(SceneObjectId{1}, {1.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}));
		window.selectedSceneArrows = {SceneObjectId{1}};
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		const glm::vec3 pivot = ComputeTransformPivot(
			TransformPivotMode::Median, SceneTransformPivotPositions(snapshot), std::nullopt);
		SceneTransformDelta delta;
		delta.spatial.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Rotate, TransformPivotMode::Median, pivot);

		ExpectVec3Near(window.sceneArrows[0].start, glm::vec3(2.0f, -1.0f, 0.0f));
		ExpectVec3Near(window.sceneArrows[0].end, glm::vec3(2.0f, 1.0f, 0.0f));
	}

	TEST(SceneTransformTests, ArrowScaleAlongAxisMovesEndpointsAndCancelRestoresGeometry)
	{
		RendererWindowState window;
		RendererWindowState::SceneArrow arrow =
			MakeArrow(SceneObjectId{8}, {1.0f, 2.0f, 2.0f}, {3.0f, 4.0f, 4.0f});
		arrow.style.shaftWidth = 0.08f;
		arrow.style.headWidth = 0.18f;
		arrow.style.headLength = 0.27f;
		window.sceneArrows.push_back(arrow);
		window.selectedSceneArrows = {arrow.id};
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		const glm::vec3 pivot = ComputeTransformPivot(
			TransformPivotMode::Median, SceneTransformPivotPositions(snapshot), std::nullopt);

		ModalTransformSession session = BeginModalTransform(
			ModalTransformOp::Scale, TransformOrientation::Global, TransformBases{}, pivot, glm::vec2(0.0f));
		session.constraint = TransformConstraint{
			ConstraintKind::Axis, 2, TransformOrientation::Global};
		session.numericText = "2";
		SceneTransformDelta delta;
		delta.spatial = EvaluateModalTransform(
			session, ModalTransformView{}, glm::vec2(0.0f), SnapMode::Off, TransformSnapSteps{});
		delta.scaleFactor = 2.0f;

		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Scale, TransformPivotMode::Median, pivot);

		ExpectVec3Near(window.sceneArrows[0].start, glm::vec3(1.0f, 2.0f, 1.0f));
		ExpectVec3Near(window.sceneArrows[0].end, glm::vec3(3.0f, 4.0f, 5.0f));
		EXPECT_FLOAT_EQ(window.sceneArrows[0].style.shaftWidth, arrow.style.shaftWidth);
		EXPECT_FLOAT_EQ(window.sceneArrows[0].style.headWidth, arrow.style.headWidth);
		EXPECT_FLOAT_EQ(window.sceneArrows[0].style.headLength, arrow.style.headLength);

		RestoreSceneTransformSelection(window, snapshot);
		ExpectVec3Near(window.sceneArrows[0].start, arrow.start);
		ExpectVec3Near(window.sceneArrows[0].end, arrow.end);
	}

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

	TEST(SceneTransformTests, MixedAtomAndArrowConfirmCreatesOneUndoEntryAndRestoresBoth)
	{
		RendererWindowState window;
		window.windowId = "mixed";
		window.structure.atoms.emplace_back();
		window.structure.atoms[0].cartesianPosition = glm::vec3(0.0f);
		window.selectedAtomIndices = {0};
		window.sceneArrows.push_back(MakeArrow(SceneObjectId{3}, {0.0f, 1.0f, 0.0f}, {0.0f, 2.0f, 0.0f}));
		window.selectedSceneArrows = {SceneObjectId{3}};
		const glm::vec3 atomBefore = window.structure.atoms[0].cartesianPosition;
		const SceneObjectsSnapshot sceneBefore = CaptureSceneObjectsSnapshot(window);
		const SceneTransformSelectionSnapshot transformBefore = CaptureSceneTransformSelection(window);
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(2.0f, 0.0f, 0.0f);
		ApplySceneTransformSelection(
			window, transformBefore, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));

		UndoStack stack;
		auto group = stack.ScopedGroup("Move selection");
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			[&window](const std::string &) { return &window; }, window.windowId, sceneBefore)));
		ASSERT_TRUE(stack.PushExecuted(CreateUnique<AtomPositionSnapshotCommand>(window, atomBefore)));
		ASSERT_TRUE(group.Commit().HasValue());
		ASSERT_EQ(stack.GetUndoDepth(), 1u);

		ASSERT_TRUE(stack.Undo().HasValue());
		ExpectVec3Near(window.structure.atoms[0].cartesianPosition, atomBefore);
		ExpectVec3Near(window.sceneArrows[0].start, sceneBefore.sceneArrows[0].start);
		ExpectVec3Near(window.sceneArrows[0].end, sceneBefore.sceneArrows[0].end);
	}
	// --- Task 31 #14: the gizmo must move orbitals and planes, not only atoms, labels and arrows.

	namespace
	{
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
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Scale, TransformPivotMode::Median, glm::vec3(0.0f));

		EXPECT_NEAR(window.sceneOrbitals[0].scale, 2.0f, kEpsilon);
		EXPECT_NEAR(window.scenePlanes[0].halfExtents.x, 4.0f, kEpsilon);
		EXPECT_NEAR(window.scenePlanes[0].halfExtents.y, 2.0f, kEpsilon);
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
		EXPECT_NEAR(glm::length(plane.normal), 1.0f, kEpsilon);
		EXPECT_NEAR(glm::length(plane.tangent), 1.0f, kEpsilon);
		EXPECT_NEAR(glm::dot(plane.normal, plane.tangent), 0.0f, kEpsilon);
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
