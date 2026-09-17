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
	class DomainLayer;
	class EventBus;

	class RendererPanel final : public IPanel
	{
	public:
		explicit RendererPanel(
			RendererLayer &layer,
			Ref<EventBus> eventBus,
			WeakRef<ContextManager> contextManager,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<DomainLayer> domainLayer,
			std::string title = "Renderer",
			bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Scene; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void render(float deltaTime);
		void renderStructureWindow(
			RendererWindowState &windowState, float deltaTime, std::vector<std::string> &windowsToClose);
		void handleMeasureToolClick(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered);
		// Region select, its hit-tests and the label/arrow gizmos moved to ViewportSelection.hpp -
		// the creation panes are not RendererPanel windows, so as members none of it ran there.
		void drawPeriodicTableWindow();
		// Modal opened by the vertical toolbar's "Add" button (drawViewportVerticalToolbar) - state
		// lives here rather than per-window since only one instance can be open at a time.
		void drawAddAtomPopup();
		// Blender-style Shift+A/"Add" toolbar button menu - see m_AddMenuRequested's comment.
		void drawAddMenu();
		void consumeAddMenuRequests();
		[[nodiscard]] bool handleCursor3DPlacement(
			RendererWindowState &windowState, float relX, float relY);
		[[nodiscard]] glm::vec3 computeViewportWorldPosition(
			const RendererWindowState &windowState, float relX, float relY) const;
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
		WeakRef<DomainLayer> m_DomainLayer;
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
