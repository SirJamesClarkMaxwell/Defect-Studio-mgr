#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>
#include <utility>

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
} // namespace DefectStudio::Tests
