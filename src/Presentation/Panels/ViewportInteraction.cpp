// The shared per-frame viewport mouse pipeline. Exists so there is exactly one definition of the
// order these run in: RendererPanel and StructureCreationTabsPanel both call it instead of each
// keeping its own copy of the chain, which is what let the creation panes drift feature by feature.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportInteraction.hpp"

#include <algorithm>

#include <imgui.h>

#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportNavigationGizmo.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportTextEditor.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"

namespace DefectStudio
{
	bool RunViewportGizmoChain(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		bool hovered,
		const float horizontalToolbarOffset,
		RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandRegistry)
	{
		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		RefreshAnchoredFreeLabels(windowState);
		ResolveAnchoredOrbitals(windowState);
		ResolveAnchoredScenePlanes(windowState);
		// Keeps each label entity's TransformComponent current before the gizmo/hit-test below read
		// it - anchors move every frame with the atoms they measure (gizmo drag, nudge, relaxation
		// playback), so a stale transform would visibly lag a frame behind the label's own draw.
		SceneSystem::UpdateLabelTransforms(windowState.sceneRegistry, windowState);

		// Keyboard-only pin shortcuts (F flip / Delete / Ctrl+Shift+</>) run unconditionally - they
		// have no mouse hit-test of their own, so short-circuiting them behind an earlier gizmo's
		// mouse-capture would silently drop them whenever the mouse happens to be hovering that
		// gizmo's pick band.
		if (IsViewportTextEditorActive(windowState))
		{
			DrawViewportTextEditor(windowState, imageOrigin, imageSize);
			return true;
		}
		if (ImGui::GetIO().WantTextInput)
			return true;
		if (hovered && !windowState.pathEdit.IsActive() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			HandleFreeLabelInteraction(windowState, imageOrigin, imageSize, hovered))
			return true;
		if (windowState.activeSelectionTool == SelectionToolMode::Text)
			return RenderViewportNavigationGizmo(windowState, imageOrigin, imageSize, hovered, horizontalToolbarOffset, layer) ||
				HandleFreeLabelInteraction(windowState, imageOrigin, imageSize, hovered);
		HandlePinnedMeasurementKeyboardShortcuts(windowState, hovered, layer);

		// Short-circuiting `||` is intentional (unlike the keyboard call above): each function's
		// mouse click/drag-start logic must NOT also run once an earlier one already claimed this
		// frame's click - e.g. clicking an atom gizmo handle must not also be reinterpreted as a pin
		// pick by HandlePinnedMeasurementInteraction's own hit-test underneath it.
		const bool editedPathMarkerClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			windowState.pathEdit.IsActive() &&
			IsScenePathMarkerUnderMouse(windowState, imageOrigin, imageSize);
		return RenderViewportNavigationGizmo(
			windowState, imageOrigin, imageSize, hovered, horizontalToolbarOffset, layer) ||
			// A path marker gets first refusal in Edit Mode. This keeps the smaller marker hitbox ahead
			// of the gizmo's longer axis hitbox, while non-marker clicks still reach the gizmo below.
			(editedPathMarkerClicked && HandleScenePathInteraction(windowState, imageOrigin, imageSize, hovered)) ||
			RenderTransformGizmo(windowState, imageOrigin, imageSize, hovered, layer, commandRegistry) ||
			HandlePinnedMeasurementInteraction(windowState, imageOrigin, imageSize, hovered) ||
			HandleFreeLabelInteraction(windowState, imageOrigin, imageSize, hovered) ||
			HandleSceneArrowInteraction(windowState, imageOrigin, imageSize, hovered) ||
			HandleVacancyInteraction(windowState, imageOrigin, imageSize, hovered) ||
			HandleSceneOrbitalInteraction(windowState, imageOrigin, imageSize, hovered) ||
			HandleScenePathInteraction(windowState, imageOrigin, imageSize, hovered) ||
			HandleScenePlaneInteraction(windowState, imageOrigin, imageSize, hovered);
	}

	bool DrawAndDispatchSelectionTools(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		bool hovered,
		bool gizmoCapturing,
		RendererLayer &layer)
	{
		if (windowState.activeSelectionTool == SelectionToolMode::Box && windowState.selectionDragActive)
		{
			ImDrawList *drawList = ImGui::GetWindowDrawList();
			const ImVec2 start(
				imageOrigin.x + windowState.selectionDragStart.x,
				imageOrigin.y + windowState.selectionDragStart.y);
			const ImVec2 current(
				imageOrigin.x + windowState.selectionDragCurrent.x,
				imageOrigin.y + windowState.selectionDragCurrent.y);
			const ImVec2 rectMin(std::min(start.x, current.x), std::min(start.y, current.y));
			const ImVec2 rectMax(std::max(start.x, current.x), std::max(start.y, current.y));
			drawList->AddRectFilled(rectMin, rectMax, IM_COL32(255, 200, 60, 40));
			drawList->AddRect(rectMin, rectMax, IM_COL32(255, 200, 60, 255));
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::Circle && hovered)
		{
			// Brush cursor: always follows the live mouse position at the persistent,
			// scroll-adjustable radius - not a drag-defined shape like box-select.
			const ImVec2 mousePos = ImGui::GetMousePos();
			ImDrawList *drawList = ImGui::GetWindowDrawList();
			drawList->AddCircleFilled(mousePos, windowState.circleSelectRadius, IM_COL32(255, 200, 60, 40));
			drawList->AddCircle(mousePos, windowState.circleSelectRadius, IM_COL32(255, 200, 60, 255));
		}

		if (hovered && windowState.activeSelectionTool == SelectionToolMode::Circle)
		{
			ImGuiIO &io = ImGui::GetIO();
			if (io.MouseWheel != 0.0f)
			{
				constexpr float kRadiusStep = 6.0f;
				constexpr float kMinRadius = 6.0f;
				constexpr float kMaxRadius = 400.0f;
				windowState.circleSelectRadius = std::clamp(
					windowState.circleSelectRadius + io.MouseWheel * kRadiusStep, kMinRadius, kMaxRadius);
				io.MouseWheel = 0.0f;
			}
		}

		// A live gizmo drag/hover owns the viewport for the frame: grabbing a handle must not also
		// start a box drag underneath it.
		if (gizmoCapturing)
			return false;

		if (windowState.activeSelectionTool == SelectionToolMode::Box)
		{
			HandleBoxSelectDrag(windowState, imageOrigin, hovered, layer);
			return true;
		}
		if (windowState.activeSelectionTool == SelectionToolMode::Circle)
		{
			HandleCircleSelectDrag(windowState, imageOrigin, hovered, layer);
			return true;
		}
		return false;
	}
} // namespace DefectStudio
