#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "App/CreationSession.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class EventBus;

	// The renderer half of a structure creation session: one tab per session, each hosting the fixed
	// 2+1 preview layout (unit cell and supercell side by side on top, a full-width analysis view
	// below) plus the toolbar shared by all three.
	//
	// The three views are ephemeral renderer windows tagged with the session id - no StructureId,
	// nothing in the domain. They are docked here by DockBuilder rather than into the main central
	// node, and they close with the session.
	//
	// The layout is fixed on purpose. Two comparable views plus one wide view is the whole reason the
	// arrangement exists; making it configurable would buy nothing and cost a persisted layout schema.
	class StructureCreationTabsPanel final : public IPanel
	{
	public:
		StructureCreationTabsPanel(
			RendererLayer &rendererLayer,
			Ref<CreationSessionRegistry> sessionRegistry,
			Ref<EventBus> eventBus,
			ElementPropertiesTable elementPropertiesTable,
			AtomStyleTable atomStyleTable,
			std::string title = "Structure Creation",
			bool visibleByDefault = true);
		StructureCreationTabsPanel(const StructureCreationTabsPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void renderSession(CreationSession &session);
		void drawSharedToolbar(CreationSession &session);
		// Rebuilds the three preview windows, but only when something they depend on actually
		// changed: a per-frame rebuild would re-run bond generation against a supercell every repaint.
		void refreshPreviews(CreationSession &session);
		[[nodiscard]] std::size_t computePreviewSignature(const CreationSession &session) const;
		void buildDockLayout(const CreationSession &session, unsigned int dockspaceId);
		void closeSessionWindows(CreationSession &session);

		RendererLayer &m_RendererLayer;
		Ref<CreationSessionRegistry> m_SessionRegistry;
		Ref<EventBus> m_EventBus;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;

		std::unordered_map<std::string, std::size_t> m_PreviewSignatures;
		std::unordered_set<std::string> m_LaidOutSessions;
	};
} // namespace DefectStudio
