#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Core/Utils/Input.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Renderer/Scene/SceneObject.hpp" // SceneObjectId, for the path hit-tests below

namespace DefectStudio
{
	class CommandRegistry;
	class RendererLayer;
	struct RendererWindowState;
	struct LabelPickQuad;

	[[nodiscard]] std::optional<bool> MouseInLabelQuad(const RendererWindowState &windowState,
		const std::vector<LabelPickQuad> &quads, std::size_t index, const ImVec2 &imageOrigin,
		const ImVec2 &imageSize, const glm::vec2 &mouse);

	// Registers commands whose target is the currently focused viewport and whose implementation
	// lives with the scene-object interaction code below.
	void RegisterViewportSceneObjectCommands(CommandRegistry &registry, RendererLayer &rendererLayer);

	[[nodiscard]] inline bool IsUnmodifiedModalAxisKeyPressed(ImGuiKey key)
	{
		const KeyModifiers modifiers = Input::GetCurrentKeyModifiers();
		if (HasModifier(modifiers, KeyModifiers::Ctrl) || HasModifier(modifiers, KeyModifiers::Alt))
			return false;
		return ImGui::IsKeyPressed(key, false);
	}

	// Box/circle region select and annotation click/drag handling used to live as
	// RendererPanel members. A viewport is not always a
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
	// Paths return ids, not indices: a path lives in a PathStore and has no index for the caller
	// to hold on to. Both sample the polyline the render pass last built (PathCaches::FindLastBuilt)
	// rather than re-tessellating, the same reason PickFrontmostScenePath does - a region that
	// catches a curve the screen does not show is worse than one that misses it.
	//   ponytail: a path the render pass has never reached is not region-selectable, exactly as it
	//   is not click-selectable. Same one-frame window, same upgrade path.
	// Hidden and non-renderable paths are skipped, so a box drawn over a hidden path selects
	// nothing - matching the click behaviour rather than the vector kinds', which do not check.
	[[nodiscard]] std::vector<SceneObjectId> HitTestRectScenePaths(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax);
	[[nodiscard]] std::vector<SceneObjectId> HitTestCircleScenePaths(
		const RendererWindowState &windowState, glm::vec2 center, float radius);
	void ApplyPathElementRectSelection(
		RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax,
		RendererEvents::Viewport::RegionSelectMode mode);
	void ApplyPathElementCircleSelection(
		RendererWindowState &windowState, glm::vec2 center, float radius,
		RendererEvents::Viewport::RegionSelectMode mode);

	// `pathHits` arrives as ids because that is what the path hit-tests return; the other two are
	// indices into their vectors. Defaulted so the two existing call sites and any caller that has
	// no paths stay unchanged.
	void ApplyLabelRegionSelection(
		RendererWindowState &windowState, const std::vector<std::size_t> &pinnedHits,
		const std::vector<std::size_t> &freeHits,
		RendererEvents::Viewport::RegionSelectMode mode,
		const std::vector<SceneObjectId> &pathHits = {});
	[[nodiscard]] RendererEvents::Viewport::RegionSelectMode ResolveRegionSelectMode(bool additive, bool subtractive);
	void PublishRegionSelection(
		RendererWindowState &windowState,
		std::vector<std::size_t> atomIndices,
		std::vector<std::size_t> bondIndices,
		RendererEvents::Viewport::RegionSelectMode mode,
		RendererLayer &layer);

	[[nodiscard]] bool HandlePinnedMeasurementInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	// F flip / Ctrl+Shift+</> scale-step for selected pins, plus Delete for selected pins, free
	// labels, paths, orbitals and planes. Keyboard-only, no mouse hit-test, so unlike the click/drag
	// handlers it must run every frame regardless of whether a gizmo captured this frame's mouse.
	void HandlePinnedMeasurementKeyboardShortcuts(
		RendererWindowState &windowState, bool hovered, RendererLayer &layer);
	[[nodiscard]] std::optional<SceneObjectEditKind> ResolveSelectedDrawingKind(
		const RendererWindowState &windowState);
	[[nodiscard]] bool HandleFreeLabelInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	// Click-select for sceneOrbitals: plain click replaces, Ctrl-click toggles, Shift-click adds.
	// Tests the drawn orbital surface, no drag - an orbital is moved with the transform gizmo, because a
	// stray drag detaching one from the atom it sits on would be the wrong default.
	[[nodiscard]] bool HandleSceneOrbitalInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);

	// Click-select for the window's paths: Object Mode picks the frontmost whole path; Edit Mode
	// routes clicks to the one session path and selects its elements with PickPath.
	[[nodiscard]] bool HandleScenePathInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
	[[nodiscard]] bool IsScenePathMarkerUnderMouse(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize);

	// Click-select for scenePlanes: ray against the drawn quad (PickScenePlane), no drag. Runs last
	// in the chain because a plane is usually the backdrop everything else is drawn in front of.
	[[nodiscard]] bool HandleScenePlaneInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
} // namespace DefectStudio
