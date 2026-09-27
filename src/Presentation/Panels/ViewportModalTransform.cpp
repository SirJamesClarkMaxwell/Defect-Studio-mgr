#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportModalTransform.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] ModalTransformOp ToModalOperation(GizmoOperation operation)
		{
			switch (operation)
			{
				case GizmoOperation::Translate: return ModalTransformOp::Translate;
				case GizmoOperation::Rotate: return ModalTransformOp::Rotate;
				case GizmoOperation::Scale: return ModalTransformOp::Scale;
			}
			return ModalTransformOp::Translate;
		}

		[[nodiscard]] std::string_view CommitDescription(ModalTransformOp operation)
		{
			switch (operation)
			{
				case ModalTransformOp::Translate: return "Move selection";
				case ModalTransformOp::Rotate: return "Rotate selection";
				case ModalTransformOp::Scale: return "Scale selection";
			}
			return "Transform selection";
		}

		[[nodiscard]] TransformSnapSteps SnapSteps(const RendererLayer &layer)
		{
			const RendererViewportSettings &viewport = layer.GetGlobalSettings().viewport;
			return {
				viewport.transformTranslateSnap,
				viewport.transformRotateSnapDegrees,
				viewport.transformScaleSnap};
		}

		void ResetModalTransform(RendererWindowState &windowState)
		{
			windowState.modalTransform.reset();
			windowState.modalTransformSelection = {};
			windowState.modalTransformSceneObjectsBefore.reset();
			windowState.modalTransformStartedFromHandle = false;
			windowState.gizmoDragActive = false;
		}

		void CaptureNumericInput(ModalTransformSession &session)
		{
			for (int digit = 0; digit < 10; ++digit)
			{
				if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit), false) ||
					ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit), false))
					AppendNumericChar(session, static_cast<char>('0' + digit));
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
				AppendNumericChar(session, '-');
			if (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false))
				AppendNumericChar(session, '.');
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
				EraseNumericChar(session);
		}

		[[nodiscard]] std::optional<glm::vec2> ProjectAbsolute(
			const ModalTransformView &view, const glm::vec3 &world)
		{
			const std::optional<glm::vec2> relative =
				SelectionHitTest::ProjectToScreen(view.projection * view.view, view.viewportSize, world);
			return relative.has_value() ? std::optional<glm::vec2>(view.viewportOrigin + *relative) : std::nullopt;
		}

		void DrawConstraintLine(
			const ModalTransformView &view, const glm::vec3 &pivot, const glm::vec3 &worldDirection, ImU32 color)
		{
			if (glm::dot(worldDirection, worldDirection) <= 1.0e-8f)
				return;
			const std::optional<glm::vec2> pivotScreen = ProjectAbsolute(view, pivot);
			if (!pivotScreen.has_value())
				return;

			const glm::vec3 direction = glm::normalize(worldDirection);
			std::optional<glm::vec2> probe = ProjectAbsolute(view, pivot + direction);
			glm::vec2 screenDirection(0.0f);
			if (probe.has_value())
				screenDirection = *probe - *pivotScreen;
			if (glm::dot(screenDirection, screenDirection) <= 1.0f)
			{
				probe = ProjectAbsolute(view, pivot - direction);
				if (probe.has_value())
					screenDirection = *pivotScreen - *probe;
			}
			if (glm::dot(screenDirection, screenDirection) <= 1.0f)
				return;

			screenDirection = glm::normalize(screenDirection);
			ImGui::GetForegroundDrawList()->AddLine(
				ImVec2(pivotScreen->x - screenDirection.x * 10000.0f, pivotScreen->y - screenDirection.y * 10000.0f),
				ImVec2(pivotScreen->x + screenDirection.x * 10000.0f, pivotScreen->y + screenDirection.y * 10000.0f),
				color, 3.0f);
		}

		void DrawModalOverlay(
			const ModalTransformSession &session, const TransformDelta &delta, SnapMode snap,
			const TransformSnapSteps &steps, const ModalTransformView &view)
		{
			if (session.constraint.kind != ConstraintKind::None && session.constraint.axis >= 0 &&
				session.constraint.axis < 3)
			{
				ImDrawList *drawList = ImGui::GetForegroundDrawList();
				drawList->PushClipRect(
					ImVec2(view.viewportOrigin.x, view.viewportOrigin.y),
					ImVec2(view.viewportOrigin.x + view.viewportSize.x, view.viewportOrigin.y + view.viewportSize.y), true);
				const glm::mat3 basis = ResolveBasis(session.constraint.space, session.bases);
				for (int axis = 0; axis < 3; ++axis)
				{
					const bool draw = session.constraint.kind == ConstraintKind::Axis
						? axis == session.constraint.axis
						: axis != session.constraint.axis;
					if (draw)
					{
						const glm::vec3 &lineOrigin = session.op == ModalTransformOp::Rotate
							? session.pivot
							: session.anchor;
						const ImU32 color = (ViewportTransformAxisColor(axis) & IM_COL32(255, 255, 255, 0)) |
							IM_COL32(0, 0, 0, 220);
						DrawConstraintLine(view, lineOrigin, basis[axis], color);
					}
				}
				drawList->PopClipRect();
			}

			const std::string header = FormatModalTransformHeader(session, delta, snap, steps);
			ImGui::GetForegroundDrawList()->AddText(
				ImVec2(view.viewportOrigin.x + 12.0f, view.viewportOrigin.y + 12.0f),
				IM_COL32(255, 240, 150, 255), header.c_str());
		}

		[[nodiscard]] float RotationAmount(
			const ModalTransformSession &session, SnapMode snap, const TransformSnapSteps &steps)
		{
			if (!session.numericText.empty())
				return glm::radians(NumericValue(session).value_or(0.0f));
			float degrees = glm::degrees(session.accumulatedAngleRadians);
			const float step = ResolveSnapStep(ModalTransformOp::Rotate, snap, steps);
			if (step > 0.0f)
				degrees = SnapValue(degrees, step);
			return glm::radians(degrees);
		}

		[[nodiscard]] float ScaleFactor(
			const TransformDelta &delta, const ModalTransformSession &session)
		{
			const TransformOrientation space = session.constraint.kind == ConstraintKind::None
				? session.orientation
				: session.constraint.space;
			const glm::mat3 basis = ResolveBasis(space, session.bases);
			if (std::abs(glm::determinant(basis)) <= 1.0e-6f)
				return 1.0f;
			const glm::mat3 local = glm::inverse(basis) * delta.linear * basis;
			if (session.constraint.kind == ConstraintKind::Axis)
				return local[session.constraint.axis][session.constraint.axis];
			if (session.constraint.kind == ConstraintKind::Plane)
			{
				for (int axis = 0; axis < 3; ++axis)
					if (axis != session.constraint.axis)
						return local[axis][axis];
			}
			return local[0][0];
		}

		void CommitAtomPreview(
			RendererWindowState &windowState, const SceneTransformSelectionSnapshot &snapshot,
			const WeakRef<CommandRegistry> &commandRegistryRef, std::string_view description)
		{
			if (windowState.structure.domainStructureId.empty())
				return;
			Ref<CommandRegistry> commandRegistry = commandRegistryRef.lock();
			if (commandRegistry == nullptr)
				return;

			GizmoTransformPayload payload;
			payload.windowId = windowState.windowId;
			payload.description = description;
			for (const AtomTransformStart &start : snapshot.atoms)
			{
				if (start.index >= windowState.structure.atoms.size())
					continue;
				payload.atomIndices.push_back(start.index);
				payload.afterPositions.push_back(windowState.structure.atoms[start.index].cartesianPosition);
			}
			if (payload.atomIndices.empty())
				return;

			CommandContext context;
			context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
			Result<CommandOutcome> result =
				commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
			if (!result)
				DS_LOG_WARN("Gizmo transform commit failed: {}", result.Error().technicalDetails);
		}

		void CommitTransform(
			RendererWindowState &windowState, RendererLayer &layer,
			const WeakRef<CommandRegistry> &commandRegistryRef, std::string_view description)
		{
			const bool hasAtoms = HasAtomTransformTargets(windowState.modalTransformSelection);
			const bool hasSceneObjects = HasSceneObjectTransformTargets(windowState.modalTransformSelection) &&
				windowState.modalTransformSceneObjectsBefore.has_value();
			Ref<UndoStack> undoStack = layer.GetUndoStackHandle().lock();
			std::optional<UndoScope> group;
			if (hasAtoms && hasSceneObjects && undoStack != nullptr)
				group.emplace(*undoStack, std::string(description));

			if (hasSceneObjects)
				PushSceneObjectsUndoSnapshot(windowState, std::move(*windowState.modalTransformSceneObjectsBefore));
			if (hasAtoms)
				CommitAtomPreview(windowState, windowState.modalTransformSelection, commandRegistryRef, description);
			if (group.has_value())
			{
				const Result<void> committed = group->Commit();
				if (!committed)
					DS_LOG_WARN("Transform undo group commit failed: {}", committed.Error().technicalDetails);
			}
		}
	} // namespace

	void BeginViewportModalTransform(
		RendererWindowState &windowState, ModalTransformOp op, const glm::vec2 &mouse,
		std::optional<int> axis, bool startedFromHandle, bool planeConstraint)
	{
		if (windowState.camera == nullptr || windowState.modalTransform.has_value())
			return;

		SceneTransformSelectionSnapshot snapshot =
			CaptureSceneTransformSelectionForOperation(windowState, op);
		const std::vector<glm::vec3> pivotPositions = SceneTransformPivotPositions(windowState, snapshot);
		const std::vector<glm::vec3> anchorPositions = SceneTransformAnchorPositions(windowState, snapshot);
		if (pivotPositions.empty())
			return;

		const std::optional<glm::vec3> cursor = windowState.cursor3DPlaced
			? std::optional<glm::vec3>(windowState.cursor3DPosition)
			: std::nullopt;
		const glm::vec3 pivot = ComputeTransformPivot(windowState.transformPivotMode, pivotPositions, cursor);
		const glm::vec3 anchor = anchorPositions.empty()
			? pivot
			: ComputeTransformPivot(windowState.transformPivotMode, anchorPositions, cursor);
		TransformBases bases;
		bases.local = SceneTransformLocalBasis(snapshot);
		bases.lattice = windowState.structure.lattice;
		windowState.modalTransform = BeginModalTransform(op, windowState.transformOrientation, bases, pivot, mouse);
		windowState.modalTransform->anchor = anchor;
		if (axis.has_value())
			windowState.modalTransform->constraint = CycleConstraint(
				{}, *axis, planeConstraint, windowState.transformOrientation, bases);
		windowState.modalTransformSelection = std::move(snapshot);
		if (HasSceneObjectTransformTargets(windowState.modalTransformSelection))
			windowState.modalTransformSceneObjectsBefore = CaptureSceneObjectsSnapshot(windowState);
		windowState.modalTransformStartedFromHandle = startedFromHandle;
		windowState.gizmoDragActive = true;
	}

	bool UpdateViewportModalTransform(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		const bool startRequested = std::exchange(windowState.modalTransformStartRequested, false);
		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		if (startRequested && !windowState.modalTransform.has_value())
			BeginViewportModalTransform(windowState, ToModalOperation(windowState.gizmoOperation), mouse);
		if (!windowState.modalTransform.has_value())
			return false;

		ImGui::GetIO().WantCaptureKeyboard = true;
		windowState.gizmoDragActive = true;
		ModalTransformSession &session = *windowState.modalTransform;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			RestoreSceneTransformSelection(windowState, windowState.modalTransformSelection);
			ResetModalTransform(windowState);
			return true;
		}

		constexpr std::array<ImGuiKey, 3> axisKeys = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
		for (int axis = 0; axis < 3; ++axis)
		{
			if (IsUnmodifiedModalAxisKeyPressed(axisKeys[axis]))
			{
				session.constraint = CycleConstraint(
					session.constraint, axis, ImGui::GetIO().KeyShift, session.orientation, session.bases);
				break;
			}
		}
		CaptureNumericInput(session);

		const TransformSnapSteps steps = SnapSteps(layer);
		const SnapMode snap = SnapModeFromModifiers(ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
		const ModalTransformView view{
			windowState.camera->ViewMatrix(), windowState.camera->ProjectionMatrix(),
			glm::vec2(imageOrigin.x, imageOrigin.y), glm::vec2(imageSize.x, imageSize.y)};
		SceneTransformDelta delta;
		delta.spatial = EvaluateModalTransform(session, view, mouse, snap, steps);
		delta.rotationRadians = RotationAmount(session, snap, steps);
		delta.scaleFactor = ScaleFactor(delta.spatial, session);
		ApplySceneTransformSelection(
			windowState, windowState.modalTransformSelection, delta, session.op,
			windowState.transformPivotMode, session.pivot);
		DrawModalOverlay(session, delta.spatial, snap, steps, view);

		const bool enterPressed =
			ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
		const bool confirmed = enterPressed || (windowState.modalTransformStartedFromHandle
			? !ImGui::IsMouseDown(ImGuiMouseButton_Left)
			: ImGui::IsMouseClicked(ImGuiMouseButton_Left));
		if (!confirmed)
			return true;

		CommitTransform(windowState, layer, commandRegistryRef, CommitDescription(session.op));
		ResetModalTransform(windowState);
		return true;
	}
} // namespace DefectStudio
