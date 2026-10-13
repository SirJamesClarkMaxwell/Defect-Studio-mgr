#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <imgui.h>

namespace DefectStudio
{
	PhononModesPanel::PhononModesPanel(Ref<PhononPanelSession> session, View view,
		std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Session(std::move(session)), m_View(view) {}

	Ref<IPanel> PhononModesPanel::Clone() const
	{
		return CreateRef<PhononModesPanel>(*this);
	}

	void PhononModesPanel::SetPersistedState(PhononPanelSettings settings, Path projectDirectory)
	{
		m_Session->SetPersistedState(std::move(settings), std::move(projectDirectory));
		m_FolderEdits.clear();
	}

	void PhononModesPanel::SetProjectRoots(std::vector<ProjectRootEntry> roots)
	{
		m_Session->SetProjectRoots(std::move(roots));
	}

	void PhononModesPanel::SetDirectory(const std::string &role, const Path &directory)
	{
		m_Session->SetDirectory(role, directory);
	}

	void PhononModesPanel::StopAnimation() { m_Session->StopAnimation(); }
	void PhononModesPanel::persist() { m_Session->Changed(); }

	void PhononModesPanel::selectMode(int index)
	{
		m_Session->SelectMode(index);
		m_ScrollToMode = true;
		persist();
	}

	void PhononModesPanel::Render()
	{
		if (!IsVisible()) return;
		ImGui::SetNextWindowSize(ImVec2(950.0f, 760.0f), ImGuiCond_FirstUseEver);
		bool open = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &open);
		if (!open)
		{
			SetVisible(false);
			if (m_View == View::Main) m_Session->ShowViews(false);
		}
		else if (draw)
		{
			auto &style = ImGui::GetStyle();
			const auto mouseHoverFlags = std::exchange(style.HoverFlagsForTooltipMouse,
				style.HoverFlagsForTooltipMouse | ImGuiHoveredFlags_AllowWhenDisabled);
			const auto navHoverFlags = std::exchange(style.HoverFlagsForTooltipNav,
				style.HoverFlagsForTooltipNav | ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::PushID(m_Session->m_ProjectDirectory.Utf8().c_str());
			if (m_View == View::Main)
			{
				if (ImGui::SmallButton("Pokaż okna fononów")) m_Session->ShowViews(true);
				drawDatasets();
				drawInputs();
				drawCcd();
				drawDispersion();
			}
			else if (m_View == View::Modes)
			{
				if (m_Session->Result())
				{
					if (m_LastSelectedMode != m_Session->Settings().selectedMode) m_ScrollToMode = true;
					m_LastSelectedMode = m_Session->Settings().selectedMode;
					drawSummary();
					drawModesTable();
					drawVisualisation();
				}
				else ImGui::TextDisabled("Wczytaj wynik fononowy w oknie Fonony.");
			}
			else drawPlots();
			ImGui::PopID();
			style.HoverFlagsForTooltipMouse = mouseHoverFlags;
			style.HoverFlagsForTooltipNav = navHoverFlags;
		}
		ImGui::End();
	}
}
