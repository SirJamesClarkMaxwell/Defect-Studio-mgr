#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class ContextManager;
	class EventBus;

	class RendererPanel final : public IPanel
	{
	public:
		explicit RendererPanel(
			RendererLayer &layer,
			Ref<EventBus> eventBus,
			WeakRef<ContextManager> contextManager,
			WeakRef<CommandRegistry> commandRegistry = {},
			std::string title = "Renderer",
			bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void render(float deltaTime);
		void renderStructureWindow(
			RendererWindowState &windowState, float deltaTime, std::vector<std::string> &windowsToClose);
		void drawViewportToolbar(RendererWindowState &windowState);
		void drawViewportVerticalToolbar(RendererWindowState &windowState);
		void applyViewportInputNavigation(RendererWindowState &windowState, const ImVec2 &imageOrigin, float deltaTime);
		void applyContinuousNudge(RendererWindowState &windowState, float deltaTime);
		void applyContinuousPan(RendererWindowState &windowState, float deltaTime);
		void onViewportFocusChanged(const std::string &windowId, bool focused);
		void handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive);
		// Plain-click entry point for the main viewport (unlike handleAtomPick, which stays atom-only
		// for handleMeasureToolClick's pair/triple picking) - tries atoms first, then bonds, so an
		// atom under the cursor always wins a bond behind/near it.
		void handleViewportPick(RendererWindowState &windowState, float relX, float relY, bool additive);
		void handleMeasureToolClick(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered);
		void handleBoxSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered);
		void handleCircleSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered);
		[[nodiscard]] std::vector<std::size_t> hitTestRect(
			const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const;
		[[nodiscard]] std::vector<std::size_t> hitTestCircle(
			const RendererWindowState &windowState, glm::vec2 center, float radius) const;
		// Bond counterparts of hitTestRect/hitTestCircle above - test the bond's (periodic-offset
		// aware) midpoint against the same screen-space shape, same convention already used for a
		// pinned two-atom measurement's anchor. Box/circle select previously only ever matched atoms,
		// silently excluding bonds even though a plain click can select one - kept as separate methods
		// (not folded into the atom ones) since callers publish the two lists as different fields.
		[[nodiscard]] std::vector<std::size_t> hitTestRectBonds(
			const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const;
		[[nodiscard]] std::vector<std::size_t> hitTestCircleBonds(
			const RendererWindowState &windowState, glm::vec2 center, float radius) const;
		// Label counterparts of the above - pinned bond/angle measurements and free labels
		// (selectedPinnedMeasurements/selectedFreeLabels) are multi-select just like atoms/bonds.
		[[nodiscard]] std::vector<std::size_t> hitTestRectPinnedMeasurements(
			const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const;
		[[nodiscard]] std::vector<std::size_t> hitTestCirclePinnedMeasurements(
			const RendererWindowState &windowState, glm::vec2 center, float radius) const;
		[[nodiscard]] std::vector<std::size_t> hitTestRectFreeLabels(
			const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const;
		[[nodiscard]] std::vector<std::size_t> hitTestCircleFreeLabels(
			const RendererWindowState &windowState, glm::vec2 center, float radius) const;
		// sceneArrows are a segment (start/end), not a single point like the anchors above - "hit"
		// means either endpoint or the midpoint lands in the rect/circle (good enough for a box/
		// circle-select convenience feature, not full segment-vs-region clipping).
		[[nodiscard]] std::vector<std::size_t> hitTestRectSceneArrows(
			const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const;
		[[nodiscard]] std::vector<std::size_t> hitTestCircleSceneArrows(
			const RendererWindowState &windowState, glm::vec2 center, float radius) const;
		void applyLabelRegionSelection(
			RendererWindowState &windowState, const std::vector<std::size_t> &pinnedHits,
			const std::vector<std::size_t> &freeHits, const std::vector<std::size_t> &arrowHits,
			RendererEvents::Viewport::RegionSelectMode mode);
		[[nodiscard]] static RendererEvents::Viewport::RegionSelectMode resolveRegionSelectMode(bool additive, bool subtractive);
		void publishRegionSelection(
			RendererWindowState &windowState,
			std::vector<std::size_t> atomIndices,
			std::vector<std::size_t> bondIndices,
			RendererEvents::Viewport::RegionSelectMode mode);
		void drawPeriodicTableWindow();
		// Modal opened by the vertical toolbar's "Add" button (drawViewportVerticalToolbar) - state
		// lives here rather than per-window since only one instance can be open at a time.
		void drawAddAtomPopup();
		// Blender-style Shift+A/"Add" toolbar button menu - see m_AddMenuRequested's comment.
		void drawAddMenu();
		[[nodiscard]] bool renderTransformGizmo(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		[[nodiscard]] bool renderLabelTransformGizmo(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		[[nodiscard]] bool handlePinnedMeasurementInteraction(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		// F flip / Delete / Ctrl+Shift+</> scale-step for the selected pin - keyboard-only, no mouse
		// hit-test, so unlike handlePinnedMeasurementInteraction's click/drag half it must run every
		// frame regardless of whether a gizmo already captured this frame's mouse.
		void handlePinnedMeasurementKeyboardShortcuts(RendererWindowState &windowState, bool hovered);
		[[nodiscard]] bool handleFreeLabelInteraction(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		// Drawn translate/rotate/scale gizmo for the current SceneArrow selection - sibling of
		// renderLabelTransformGizmo above, called before handleSceneArrowInteraction below in the same
		// short-circuiting `||` chain so grabbing a gizmo handle is never also reinterpreted as a plain
		// endpoint/shaft click by that function's own proximity hit-test.
		[[nodiscard]] bool renderSceneArrowTransformGizmo(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		// Click-select + drag for sceneArrows - a plain click near an already-selected arrow's
		// start/end/midpoint (when the gizmo above didn't already claim the click) decides what the
		// following drag moves, same screen-space-proximity idea as isBondUnderScreenPosition, just
		// resolved once at click time instead of every frame.
		[[nodiscard]] bool handleSceneArrowInteraction(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		[[nodiscard]] bool handleCursor3DPlacement(
			RendererWindowState &windowState, float relX, float relY);
		[[nodiscard]] glm::vec3 computeViewportWorldPosition(
			const RendererWindowState &windowState, float relX, float relY) const;
		[[nodiscard]] bool isAtomUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos) const;
		[[nodiscard]] bool isBondUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos) const;
		void renderViewportContextMenu(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		// Blender-style "adjust last operation" popup for a just-added SceneArrow - see
		// RendererWindowState::sceneArrowQuickEditActive.
		void renderSceneArrowQuickEditPanel(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize);

	private:
		RendererLayer &m_Layer;
		Ref<EventBus> m_EventBus;
		WeakRef<ContextManager> m_ContextManager;
		WeakRef<CommandRegistry> m_CommandRegistry;
		std::unordered_map<std::string, ImVec2> m_LastMousePositions;
		// Snapshot of the right-click's world position, taken the frame the viewport context menu
		// opens (ImGui::IsWindowAppearing()) - "Set 3D cursor here" reads it later, when the user
		// actually clicks that menu item and the live mouse position no longer points at the click.
		// Only one context menu can be open at a time app-wide, so a single field is enough.
		glm::vec3 m_ContextMenuWorldPosition = glm::vec3(0.0f);

		// Add Atom popup (drawAddAtomPopup) - only one instance can be open app-wide, so single
		// fields are enough, same reasoning as m_ContextMenuWorldPosition above. Doubles as the
		// window's own open/closed state (passed as ImGui::Begin's p_open), not just a one-shot
		// "please open" request - a plain window, not a modal, see drawAddAtomPopup's comment.
		bool m_AddAtomPopupRequested = false;
		std::string m_AddAtomPopupWindowId;
		bool m_AddAtomPopupFractional = false;
		glm::vec3 m_AddAtomPopupPosition = glm::vec3(0.0f);

		// Blender-style Shift+A "what to add" menu (drawAddMenu) - opens at the mouse position with a
		// short list (Atom.../Label), picking one either opens that type's own dialog (Atom) or adds
		// it immediately (Label, same as the right-click "Add" submenu in renderViewportContextMenu).
		// One-shot request like m_AddAtomPopupRequested's toggle event, not a persistent open flag -
		// ImGui::OpenPopup/BeginPopup own the popup's actual open/closed state once shown.
		bool m_AddMenuRequested = false;
		std::string m_AddMenuWindowId;
		glm::vec3 m_AddMenuPosition = glm::vec3(0.0f);
		bool m_AddMenuPositionFractional = false;
		ImVec2 m_AddMenuScreenPos = ImVec2(0.0f, 0.0f);
	};
} // namespace DefectStudio
