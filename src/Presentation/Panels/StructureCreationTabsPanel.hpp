#pragma once

#include <string>
#include <unordered_map>

#include <imgui.h>

#include "App/CreationSession.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class ContextManager;
	class EventBus;

	// The renderer half of a structure creation session: ONE window per session, split into three
	// panes - the basis (motif) and the conventional unit cell side by side on top, the supercell
	// full width below - sharing a single horizontal and vertical viewport toolbar.
	//
	// The panes are ephemeral renderer windows tagged with the session id (no StructureId, nothing
	// in the domain), but they are never ImGui windows of their own: RendererPanel skips them and
	// this panel draws each one's FBO into a child region. That is the difference from the previous
	// DockBuilder 2+1 layout, where hiding a view destroyed a docked window mid-frame and crashed
	// inside DockNodeUpdateHasCentralNodeChild. A hidden pane now keeps its renderer window and is
	// purely a layout decision.
	//
	// Each pane keeps its own camera: rotating the supercell must not move the unit-cell view.
	// The toolbars and the mouse act on the active pane only (click a pane to activate it).
	class StructureCreationTabsPanel final : public IPanel
	{
	public:
		StructureCreationTabsPanel(
			RendererLayer &rendererLayer,
			Ref<CreationSessionRegistry> sessionRegistry,
			Ref<EventBus> eventBus,
			WeakRef<ContextManager> contextManager,
			WeakRef<CommandRegistry> commandRegistry,
			ElementPropertiesTable elementPropertiesTable,
			AtomStyleTable atomStyleTable,
			std::string title = "Structure Creation",
			bool visibleByDefault = true);
		StructureCreationTabsPanel(const StructureCreationTabsPanel &other) = default;

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Structure; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void renderSession(CreationSession &session);
		// One pane: its own FBO drawn into a child region of the session window, plus
		// click-to-activate and (when active and hovered) mouse navigation.
		void drawPane(CreationSession &session, int paneIndex, const ImVec2 &size, bool windowFocused);
		void drawPaneLayout(CreationSession &session, const ImVec2 &available, bool windowFocused);
		// Rebuilds the three preview windows, but only when something they depend on actually
		// changed: a per-frame rebuild would re-run bond generation against a supercell every repaint.
		void refreshPreviews(CreationSession &session);
		[[nodiscard]] std::size_t computePreviewSignature(const CreationSession &session) const;
		[[nodiscard]] RendererWindowState *findPaneWindow(const CreationSession &session, int paneIndex);
		void closeSessionWindows(CreationSession &session);

		RendererLayer &m_RendererLayer;
		Ref<CreationSessionRegistry> m_SessionRegistry;
		Ref<EventBus> m_EventBus;
		WeakRef<ContextManager> m_ContextManager;
		// Only for the held-arrow atom nudge, which commits through renderer.gizmo.commit_transform.
		WeakRef<CommandRegistry> m_CommandRegistry;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;

		std::unordered_map<std::string, std::size_t> m_PreviewSignatures;
		// Per-pane drag anchor for ApplyViewportInputNavigation, keyed by renderer window id.
		std::unordered_map<std::string, ImVec2> m_LastMousePositions;
	};
} // namespace DefectStudio
