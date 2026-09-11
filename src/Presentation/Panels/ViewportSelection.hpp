#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Events/RendererEvents.hpp"

namespace DefectStudio
{
	class RendererLayer;
	struct RendererWindowState;

	// Box/circle region select, the label and scene-arrow gizmos, and the click/drag handling for
	// both - all of it used to live as RendererPanel members. A viewport is not always a
	// RendererPanel window: the three-pane structure creation window draws its own, so as members
	// none of this existed there. Same regression shape as the keybindings, the atom picking and the
	// atom gizmo before them, which is why this is the last of RendererPanel's per-frame input half
	// to move out. Pure moves - the only signature change is RendererLayer arriving as a parameter.
	//
	// Callers should not run these one by one: RunViewportGizmoChain and DrawAndDispatchSelectionTools
	// in ViewportInteraction.hpp fix the order the chain depends on.

	void HandleBoxSelectDrag(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered, RendererLayer &layer);
	void HandleCircleSelectDrag(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered, RendererLayer &layer);

	[[nodiscard]] std::vector<std::size_t> HitTestRect(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax);
	[[nodiscard]] std::vector<std::size_t> HitTestCircle(
		const RendererWindowState &windowState, glm::vec2 center, float radius);
	[[nodiscard]] std::vector<std::size_t> HitTestRectBonds(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax);
	[[nodiscard]] std::vector<std::size_t> HitTestCircleBonds(
		const RendererWindowState &windowState, glm::vec2 center, float radius);
	[[nodiscard]] std::vector<std::size_t> HitTestRectPinnedMeasurements(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax);
	[[nodiscard]] std::vector<std::size_t> HitTestCirclePinnedMeasurements(
		const RendererWindowState &windowState, glm::vec2 center, float radius);
	[[nodiscard]] std::vector<std::size_t> HitTestRectFreeLabels(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax);
	[[nodiscard]] std::vector<std::size_t> HitTestCircleFreeLabels(
		const RendererWindowState &windowState, glm::vec2 center, float radius);
	[[nodiscard]] std::vector<std::size_t> HitTestRectSceneArrows(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax);
	[[nodiscard]] std::vector<std::size_t> HitTestCircleSceneArrows(
		const RendererWindowState &windowState, glm::vec2 center, float radius);

	void ApplyLabelRegionSelection(
		RendererWindowState &windowState, const std::vector<std::size_t> &pinnedHits,
		const std::vector<std::size_t> &freeHits, const std::vector<std::size_t> &arrowHits,
		RendererEvents::Viewport::RegionSelectMode mode);
	[[nodiscard]] RendererEvents::Viewport::RegionSelectMode ResolveRegionSelectMode(bool additive, bool subtractive);
	void PublishRegionSelection(
		RendererWindowState &windowState,
		std::vector<std::size_t> atomIndices,
		std::vector<std::size_t> bondIndices,
		RendererEvents::Viewport::RegionSelectMode mode,
		RendererLayer &layer);

	[[nodiscard]] bool RenderLabelTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	[[nodiscard]] bool RenderSceneArrowTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	[[nodiscard]] bool HandlePinnedMeasurementInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	// F flip / Delete / Ctrl+Shift+</> scale-step for the selected pin - keyboard-only, no mouse
	// hit-test, so unlike HandlePinnedMeasurementInteraction's click/drag half it must run every
	// frame regardless of whether a gizmo already captured this frame's mouse.
	void HandlePinnedMeasurementKeyboardShortcuts(
		RendererWindowState &windowState, bool hovered, RendererLayer &layer);
	[[nodiscard]] bool HandleFreeLabelInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	[[nodiscard]] bool HandleSceneArrowInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
} // namespace DefectStudio
