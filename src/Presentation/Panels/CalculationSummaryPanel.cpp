#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"

#include <algorithm>

#include <imgui.h>

#include "Core/Platform/FileDialog.hpp"

namespace DefectStudio
{
	CalculationSummaryPanel::CalculationSummaryPanel(Ref<CalculationSummarySession> session, View view,
		std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Session(std::move(session)), m_View(view) {}

	Ref<IPanel> CalculationSummaryPanel::Clone() const
	{
		return CreateRef<CalculationSummaryPanel>(*this);
	}

	void CalculationSummaryPanel::renderChain()
	{
		const bool loading = m_Session->isLoading();
		ImGui::BeginDisabled(loading);
		if (ImGui::Button("Reload"))
			m_Session->dispatchLoad();
		ImGui::SameLine();
		if (ImGui::Button("Add continuation..."))
		{
			const auto picked = Platform::PickFolder(m_Session->m_Directory);
			if (!picked)
				m_Session->m_Error = picked.Error().technicalDetails;
			else if (*picked)
			{
				CalculationSummarySession::Run run;
				run.directory = **picked;
				if (!std::ranges::any_of(m_Session->m_Chain, [&](const CalculationSummarySession::Run &existing)
					{ return existing.directory == run.directory && existing.outcarName == run.outcarName; }))
				{
					m_Session->m_Chain.push_back(std::move(run));
					m_Session->m_SortPending = true;
					m_Session->dispatchRun(m_Session->m_Chain.back(), m_Session->m_AutoBands ? 0 : m_Session->m_BandStart, m_Session->m_AutoBands ? 0 : m_Session->m_BandEnd);
				}
			}
		}
		if (ImGui::Button("Add saved OUTCAR..."))
		{
			const auto picked = Platform::PickOpenFile(m_Session->m_Directory, "OUTCAR (including saved copies)", "");
			if (!picked)
				m_Session->m_Error = picked.Error().technicalDetails;
			else if (*picked)
			{
				CalculationSummarySession::Run run;
				run.directory = (**picked).parent_path();
				run.outcarName = (**picked).filename().Utf8();
				if (!std::ranges::any_of(m_Session->m_Chain, [&](const CalculationSummarySession::Run &existing)
					{ return existing.directory == run.directory && existing.outcarName == run.outcarName; }))
				{
					m_Session->m_Chain.push_back(std::move(run));
					m_Session->m_SortPending = true;
					m_Session->dispatchRun(m_Session->m_Chain.back(), m_Session->m_AutoBands ? 0 : m_Session->m_BandStart, m_Session->m_AutoBands ? 0 : m_Session->m_BandEnd);
				}
			}
		}
		ImGui::EndDisabled();
		if (loading)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading...");
		}
		for (std::size_t i = 0; i < m_Session->m_Chain.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			const auto &run = m_Session->m_Chain[i];
			const std::string label = std::to_string(i + 1) + ". " + run.directory.filename().Utf8() + " / " + run.outcarName;
			if (ImGui::Selectable(label.c_str(), m_Session->m_DetailRun == static_cast<int>(i)))
				m_Session->m_DetailRun = static_cast<int>(i);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", run.directory.Utf8().c_str());
			ImGui::BeginDisabled(loading);
			ImGui::BeginDisabled(i == 0);
			if (ImGui::SmallButton("Up"))
			{
				std::swap(m_Session->m_Chain[i], m_Session->m_Chain[i - 1]);
				m_Session->m_DetailRun = static_cast<int>(i - 1);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 == m_Session->m_Chain.size());
			if (ImGui::SmallButton("Down"))
			{
				std::swap(m_Session->m_Chain[i], m_Session->m_Chain[i + 1]);
				m_Session->m_DetailRun = static_cast<int>(i + 1);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			const bool remove = ImGui::SmallButton("Remove");
			ImGui::EndDisabled();
			if (!run.error.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", run.error.c_str());
			ImGui::PopID();
			if (remove)
			{
				m_Session->m_Chain.erase(m_Session->m_Chain.begin() + static_cast<std::ptrdiff_t>(i));
				m_Session->m_DetailRun = std::max(0, static_cast<int>(m_Session->m_Chain.size()) - 1);
				break;
			}
		}
	}

	void CalculationSummaryPanel::Render()
	{
		if (!IsVisible())
			return;
		ImGui::SetNextWindowSize(ImVec2(640.0f, 800.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &stillOpen);
		if (!stillOpen)
		{
			SetVisible(false);
			if (m_View == View::Summary)
				m_Session->HideViews();
		}
		if (!draw)
		{
			ImGui::End();
			return;
		}
		if (m_Session->m_Directory.Empty())
		{
			ImGui::TextDisabled("Right-click a folder in the Project Tree and choose \"Show Calculation Summary\".");
			ImGui::End();
			return;
		}
		ImGui::SetWindowFontScale(1.2f);
		ImGui::TextDisabled("%s", m_Session->m_Directory.Utf8().c_str());
		if (m_View == View::Summary)
		{
			if (ImGui::BeginChild("##Chain", ImVec2(0.0f,
				std::max(1.0f, std::min(220.0f, 0.35f * ImGui::GetContentRegionAvail().y))), ImGuiChildFlags_Borders))
				renderChain();
			ImGui::EndChild();
		}
		else if (m_Session->isLoading())
			ImGui::TextDisabled("Loading...");
		if (!m_Session->m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Session->m_Error.c_str());
		if (m_View == View::Summary && m_Session->m_DetailRun >= 0 &&
			m_Session->m_DetailRun < static_cast<int>(m_Session->m_Chain.size()))
		{
			const auto &run = m_Session->m_Chain[m_Session->m_DetailRun];
			ImGui::SeparatorText((run.directory.filename().Utf8() + " / " + run.outcarName).c_str());
			if (run.data)
				renderSummary(*run.data);
			else
				ImGui::TextDisabled("%s", run.job ? "Loading..." : run.error.c_str());
		}
		if (m_View != View::Summary)
			renderPlots();
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
	}
} // namespace DefectStudio
