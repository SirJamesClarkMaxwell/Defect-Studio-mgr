#include "Core/dspch.hpp"

#include "Presentation/Panels/StructureCreationTabsPanel.hpp"

#include <algorithm>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "Renderer/CrystalStructurePreviewWindow.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr std::size_t PreviewWindowCount = 3;

		[[nodiscard]] std::string SupercellSuffix(const glm::ivec3 &counts)
		{
			return "(" + std::to_string(counts.x) + "x" + std::to_string(counts.y) + "x" + std::to_string(counts.z) + ")";
		}

		void HashCombine(std::size_t &seed, std::size_t value)
		{
			seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
		}
	} // namespace

	StructureCreationTabsPanel::StructureCreationTabsPanel(
		RendererLayer &rendererLayer,
		Ref<CreationSessionRegistry> sessionRegistry,
		Ref<EventBus> eventBus,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_SessionRegistry(std::move(sessionRegistry)),
		  m_EventBus(std::move(eventBus)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable))
	{
	}

	Ref<IPanel> StructureCreationTabsPanel::Clone() const
	{
		return CreateRef<StructureCreationTabsPanel>(*this);
	}

	void StructureCreationTabsPanel::Render()
	{
		if (m_SessionRegistry == nullptr)
			return;

		// Copied, not iterated in place: rendering a tab can publish RendererTabClosed, which removes
		// a session from the registry mid-loop.
		const CreationSessionRegistry::SessionList sessions = m_SessionRegistry->Sessions();
		for (const Ref<CreationSession> &session : sessions)
		{
			// A session only gets a tab once it has been handed over to the Structure Hub - while it
			// is still a Draft it lives entirely inside the New Structure panel.
			if (session != nullptr && session->state != CreationSessionState::Draft)
				renderSession(*session);
		}
	}

	void StructureCreationTabsPanel::renderSession(CreationSession &session)
	{
		const std::string sessionKey = ToString(session.sessionId);
		refreshPreviews(session);

		const std::string label = std::string(ToString(session.mode)) + " " + SupercellSuffix(session.supercellCounts)
			+ "###StructureCreationSession_" + sessionKey;

		bool open = true;
		if (ImGui::Begin(label.c_str(), &open))
		{
			drawSharedToolbar(session);

			const ImGuiID dockspaceId = ImGui::GetID("##session_dockspace");
			if (!m_LaidOutSessions.contains(sessionKey))
			{
				m_LaidOutSessions.insert(sessionKey);
				buildDockLayout(session, dockspaceId);
			}
			ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
		}
		ImGui::End();

		if (!open)
		{
			// The tab X is one of the three paths into the close protocol; the coordinator decides
			// what happens to an attempt that is still running.
			closeSessionWindows(session);
			m_LaidOutSessions.erase(sessionKey);
			m_PreviewSignatures.erase(sessionKey);

			if (m_EventBus != nullptr)
			{
				DomainEvents::RendererTabClosed closedEvent;
				closedEvent.sessionId = session.sessionId;
				m_EventBus->Publish(closedEvent);
			}
		}
	}

	void StructureCreationTabsPanel::drawSharedToolbar(CreationSession &session)
	{
		ImGui::TextUnformatted("Supercell:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::InputInt3("##session_hkl", &session.supercellCounts.x))
		{
			session.supercellCounts = glm::max(session.supercellCounts, glm::ivec3(1));
			session.lastModifiedAt = Time::Now();
		}

		// Display toggles are broadcast to all three views: comparing a unit cell against its
		// supercell is only meaningful if both are drawn the same way.
		ImGui::SameLine();
		ImGui::TextUnformatted("|");
		ImGui::SameLine();

		bool showCellBox = true;
		bool showBonds = true;
		bool showGrid = false;
		for (RendererWindowState &window : m_RendererLayer.GetWindows())
		{
			if (window.sessionId != ToString(session.sessionId))
				continue;
			showCellBox = window.showCellBox;
			showBonds = window.showBonds;
			showGrid = window.showGrid;
			break;
		}

		const auto broadcast = [&](auto apply) {
			const std::string key = ToString(session.sessionId);
			for (RendererWindowState &window : m_RendererLayer.GetWindows())
			{
				if (window.sessionId == key)
					apply(window);
			}
		};

		if (ImGui::Checkbox("Cell box", &showCellBox))
			broadcast([showCellBox](RendererWindowState &window) { window.showCellBox = showCellBox; });
		ImGui::SameLine();
		if (ImGui::Checkbox("Bonds", &showBonds))
			broadcast([showBonds](RendererWindowState &window) { window.showBonds = showBonds; });
		ImGui::SameLine();
		if (ImGui::Checkbox("Grid", &showGrid))
			broadcast([showGrid](RendererWindowState &window) { window.showGrid = showGrid; });

		ImGui::SameLine();
		ImGui::TextDisabled("| %s | %zu atoms", ToString(session.state), session.draftStructure.atoms.size());
		ImGui::Separator();
	}

	std::size_t StructureCreationTabsPanel::computePreviewSignature(const CreationSession &session) const
	{
		std::size_t seed = std::hash<std::size_t>{}(session.draftStructure.atoms.size());
		HashCombine(seed, std::hash<int>{}(session.supercellCounts.x));
		HashCombine(seed, std::hash<int>{}(session.supercellCounts.y));
		HashCombine(seed, std::hash<int>{}(session.supercellCounts.z));
		for (const AtomSite &atom : session.draftStructure.atoms)
		{
			HashCombine(seed, std::hash<std::string>{}(atom.species));
			HashCombine(seed, std::hash<float>{}(atom.fractional.x));
			HashCombine(seed, std::hash<float>{}(atom.fractional.y));
			HashCombine(seed, std::hash<float>{}(atom.fractional.z));
		}
		for (const glm::vec3 &vector : session.draftStructure.cell.vectors)
		{
			HashCombine(seed, std::hash<float>{}(vector.x));
			HashCombine(seed, std::hash<float>{}(vector.y));
			HashCombine(seed, std::hash<float>{}(vector.z));
		}
		return seed;
	}

	void StructureCreationTabsPanel::refreshPreviews(CreationSession &session)
	{
		if (session.draftStructure.atoms.empty())
			return;

		const std::string sessionKey = ToString(session.sessionId);
		const std::size_t signature = computePreviewSignature(session);
		const auto cached = m_PreviewSignatures.find(sessionKey);
		const bool alreadyOpen = session.previewWindowIds.size() == PreviewWindowCount;
		if (alreadyOpen && cached != m_PreviewSignatures.end() && cached->second == signature)
			return;

		session.previewWindowIds.resize(PreviewWindowCount);

		CrystalStructure supercell = session.draftStructure;
		if (session.supercellCounts != glm::ivec3(1))
		{
			Result<CrystalStructure> built = BuildSupercell(
				session.draftStructure,
				SupercellMatrix::Diagonal(session.supercellCounts.x, session.supercellCounts.y, session.supercellCounts.z));
			if (built)
				supercell = std::move(built.Value());
			else
				DS_LOG_WARN("Structure Creation: supercell preview failed: {}", built.Error().technicalDetails);
		}

		session.previewWindowIds[0] = ShowCrystalStructurePreview(
			session.previewWindowIds[0], session.draftStructure, "Unit cell",
			m_RendererLayer, m_ElementPropertiesTable, m_AtomStyleTable, true, false, std::nullopt, sessionKey);
		session.previewWindowIds[1] = ShowCrystalStructurePreview(
			session.previewWindowIds[1], supercell, "Supercell " + SupercellSuffix(session.supercellCounts),
			m_RendererLayer, m_ElementPropertiesTable, m_AtomStyleTable, true, false, std::nullopt, sessionKey);
		// Third view: the same structure without the cell box, for looking at the basis itself.
		session.previewWindowIds[2] = ShowCrystalStructurePreview(
			session.previewWindowIds[2], session.draftStructure, "Analysis",
			m_RendererLayer, m_ElementPropertiesTable, m_AtomStyleTable, false, true, std::nullopt, sessionKey);

		m_PreviewSignatures[sessionKey] = signature;
	}

	void StructureCreationTabsPanel::buildDockLayout(const CreationSession &session, unsigned int dockspaceId)
	{
		if (session.previewWindowIds.size() != PreviewWindowCount)
			return;

		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

		// 2 on top, 1 full-width below - the fixed layout, built once per session.
		ImGuiID topNode = 0;
		ImGuiID bottomNode = 0;
		ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Down, 0.4f, &bottomNode, &topNode);
		ImGuiID topLeftNode = 0;
		ImGuiID topRightNode = 0;
		ImGui::DockBuilderSplitNode(topNode, ImGuiDir_Right, 0.5f, &topRightNode, &topLeftNode);

		const std::array<ImGuiID, PreviewWindowCount> targets = {topLeftNode, topRightNode, bottomNode};
		for (std::size_t i = 0; i < PreviewWindowCount; ++i)
		{
			if (session.previewWindowIds[i].empty())
				continue;
			// Must match the label RendererPanel begins these windows with.
			const std::string windowLabel = "###RendererWindow_" + session.previewWindowIds[i];
			ImGui::DockBuilderDockWindow(windowLabel.c_str(), targets[i]);
		}

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void StructureCreationTabsPanel::closeSessionWindows(CreationSession &session)
	{
		for (const std::string &windowId : session.previewWindowIds)
			CloseCrystalStructurePreview(windowId, m_RendererLayer);
		session.previewWindowIds.clear();
	}
} // namespace DefectStudio
