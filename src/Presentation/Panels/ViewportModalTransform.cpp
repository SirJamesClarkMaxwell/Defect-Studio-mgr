#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportModalTransform.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>


#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
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
				case ModalTransformOp::Translate: return "Move selected atoms";
				case ModalTransformOp::Rotate: return "Rotate selected atoms";
				case ModalTransformOp::Scale: return "Scale selected atoms";
			}
			return "Transform selected atoms";
		}

		void ResetModalTransform(RendererWindowState &windowState)
		{
			windowState.modalTransform.reset();
			windowState.modalTransformAtomIndices.clear();
			windowState.modalTransformStartPositions.clear();
			windowState.modalTransformStartedFromHandle = false;
			windowState.fallbackGizmoDragging = false;
			windowState.fallbackGizmoAxis = -1;
			windowState.gizmoDragActive = false;
		}

		void RestoreStartPositions(RendererWindowState &windowState)
		{
			const std::size_t count = std::min(
				windowState.modalTransformAtomIndices.size(), windowState.modalTransformStartPositions.size());
			for (std::size_t i = 0; i < count; ++i)
			{
				const std::size_t atomIndex = windowState.modalTransformAtomIndices[i];
				if (atomIndex < windowState.structure.atoms.size())
					windowState.structure.atoms[atomIndex].cartesianPosition = windowState.modalTransformStartPositions[i];
			}
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
			constexpr std::array<ImU32, 3> colors = {
				IM_COL32(230, 70, 70, 220), IM_COL32(90, 210, 90, 220), IM_COL32(90, 150, 240, 220)};
			if (session.constraint.kind != ConstraintKind::None && session.constraint.axis >= 0 && session.constraint.axis < 3)
			{
				const glm::mat3 basis = ResolveBasis(session.constraint.space, session.bases);
				for (int axis = 0; axis < 3; ++axis)
				{
					const bool draw = session.constraint.kind == ConstraintKind::Axis
						? axis == session.constraint.axis
						: axis != session.constraint.axis;
					if (draw)
						DrawConstraintLine(view, session.pivot, basis[axis], colors[axis]);
				}
			}

			const std::string header = FormatModalTransformHeader(session, delta, snap, steps);
			ImGui::GetForegroundDrawList()->AddText(
				ImVec2(view.viewportOrigin.x + 12.0f, view.viewportOrigin.y + 12.0f),
				IM_COL32(255, 240, 150, 255), header.c_str());
		}
	} // namespace

	void BeginAtomModalTransform(
		RendererWindowState &windowState, ModalTransformOp op, const glm::vec2 &mouse,
		std::optional<int> globalAxis, bool startedFromHandle)
	{
		if (windowState.camera == nullptr || windowState.modalTransform.has_value())
			return;

		windowState.modalTransformAtomIndices.clear();
		windowState.modalTransformStartPositions.clear();
		for (const std::size_t atomIndex : windowState.selectedAtomIndices)
		{
			if (atomIndex >= windowState.structure.atoms.size())
				continue;
			windowState.modalTransformAtomIndices.push_back(atomIndex);
			windowState.modalTransformStartPositions.push_back(
				windowState.structure.atoms[atomIndex].cartesianPosition);
		}
		if (windowState.modalTransformStartPositions.empty())
			return;

		const std::optional<glm::vec3> cursor = windowState.cursor3DPlaced
			? std::optional<glm::vec3>(windowState.cursor3DPosition)
			: std::nullopt;
		const glm::vec3 pivot = ComputeTransformPivot(
			windowState.transformPivotMode, windowState.modalTransformStartPositions, cursor);
		TransformBases bases;
		bases.lattice = windowState.structure.lattice;
		windowState.modalTransform = BeginModalTransform(op, windowState.transformOrientation, bases, pivot, mouse);
		if (globalAxis.has_value())
			windowState.modalTransform->constraint =
				TransformConstraint{ConstraintKind::Axis, *globalAxis, TransformOrientation::Global};
		windowState.modalTransformStartedFromHandle = startedFromHandle;
		windowState.fallbackGizmoDragging = true;
		windowState.fallbackGizmoAxis = -1;
		windowState.gizmoDragActive = true;
	}

	void CommitAtomGizmoPreview(
		RendererWindowState &windowState, const WeakRef<CommandRegistry> &commandRegistryRef,
		std::string_view description)
	{
		if (windowState.structure.domainStructureId.empty())
			return;
		Ref<CommandRegistry> commandRegistry = commandRegistryRef.lock();
		if (commandRegistry == nullptr)
			return;

		GizmoTransformPayload payload;
		payload.windowId = windowState.windowId;
		payload.atomIndices = windowState.modalTransformAtomIndices.empty()
			? windowState.selectedAtomIndices
			: windowState.modalTransformAtomIndices;
		payload.afterPositions.reserve(payload.atomIndices.size());
		for (const std::size_t atomIndex : payload.atomIndices)
		{
			if (atomIndex < windowState.structure.atoms.size())
				payload.afterPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
		}
		if (payload.afterPositions.size() != payload.atomIndices.size())
			return;
		payload.description = description;

		CommandContext context;
		context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
		Result<CommandOutcome> result =
			commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
		if (!result)
			DS_LOG_WARN("Gizmo transform commit failed: {}", result.Error().technicalDetails);
	}

	bool UpdateAtomModalTransform(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
		const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		const bool startRequested = windowState.modalTransformStartRequested;
		windowState.modalTransformStartRequested = false;
		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		if (startRequested && !windowState.modalTransform.has_value())
			BeginAtomModalTransform(windowState, ToModalOperation(windowState.gizmoOperation), mouse);
		if (!windowState.modalTransform.has_value())
			return false;

		ImGui::GetIO().WantCaptureKeyboard = true;
		windowState.fallbackGizmoDragging = true;
		windowState.gizmoDragActive = true;
		ModalTransformSession &session = *windowState.modalTransform;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			RestoreStartPositions(windowState);
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

		const SnapMode snap = SnapModeFromModifiers(ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
		const ModalTransformView view{
			windowState.camera->ViewMatrix(), windowState.camera->ProjectionMatrix(),
			glm::vec2(imageOrigin.x, imageOrigin.y), glm::vec2(imageSize.x, imageSize.y)};
		const TransformDelta delta =
			EvaluateModalTransform(session, view, mouse, snap, windowState.transformSnapSteps);
		const std::size_t count = std::min(
			windowState.modalTransformAtomIndices.size(), windowState.modalTransformStartPositions.size());
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::size_t atomIndex = windowState.modalTransformAtomIndices[i];
			if (atomIndex >= windowState.structure.atoms.size())
				continue;
			const glm::vec3 pivot = windowState.transformPivotMode == TransformPivotMode::IndividualOrigins
				? windowState.modalTransformStartPositions[i]
				: session.pivot;
			windowState.structure.atoms[atomIndex].cartesianPosition =
				ApplyTransformDelta(delta, windowState.modalTransformStartPositions[i], pivot);
		}
		DrawModalOverlay(session, delta, snap, windowState.transformSnapSteps, view);

		const bool enterPressed =
			ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
		const bool confirmed = enterPressed || (windowState.modalTransformStartedFromHandle
			? !ImGui::IsMouseDown(ImGuiMouseButton_Left)
			: ImGui::IsMouseClicked(ImGuiMouseButton_Left));
		if (!confirmed)
			return true;

		const std::string_view description = CommitDescription(session.op);
		CommitAtomGizmoPreview(windowState, commandRegistryRef, description);
		ResetModalTransform(windowState);
		return true;
	}
} // namespace DefectStudio
