#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

#include "Core/Platform/FileDialog.hpp"

namespace DefectStudio
{
	namespace
	{
		std::uint64_t NextFocusOrder()
		{
			static std::uint64_t order = 0;
			return ++order;
		}
	}

	CalculationSummaryPanel::CalculationSummaryPanel(Ref<CalculationSummarySession> session,
		std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Session(std::move(session)), m_FocusOrder(NextFocusOrder()) {}

	Ref<IPanel> CalculationSummaryPanel::Clone() const
	{
		if (m_Closed)
			return {};
		const std::string title = GetTitle().substr(0, GetTitle().find('#')) +
			" copy##CalculationSummary" + std::to_string(NextFocusOrder());
		auto copy = CreateRef<CalculationSummaryPanel>(m_Session->clone(), title, true);
		copy->m_TopFraction = m_TopFraction;
		copy->m_LeftFraction = m_LeftFraction;
		copy->DockAsTab(*this);
		copy->RequestFocus();
		return copy;
	}

	void CalculationSummaryPanel::RequestFocus()
	{
		SetVisible(true);
		m_FocusRequested = true;
		m_FocusOrder = NextFocusOrder();
	}

	void CalculationSummaryPanel::Close()
	{
		SetVisible(false);
		m_Closed = true;
		m_Session->cancelJobs();
		m_Session->m_Chain.clear();
		m_Session->m_BandSelection.clear();
		m_Session->m_SortPending = false;
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
		ImGui::SetNextWindowSize(ImVec2(1100.0f, 850.0f), ImGuiCond_FirstUseEver);
		if (m_TabTargetTitle)
		{
			// Resolve after the older panel rendered, including two opens queued in the same frame.
			if (ImGui::FindWindowByName(m_TabTargetTitle->c_str()) != nullptr)
			{
				const auto &target = *ImGui::FindWindowByName(m_TabTargetTitle->c_str());
				m_PendingDockId = target.DockId;
				if (target.Active && target.DockId == 0)
				{
					// A floating summary needs a floating dock node before it can receive a tab.
					m_PendingDockId = ImGui::DockBuilderAddNode();
					ImGui::DockBuilderSetNodePos(*m_PendingDockId, target.Pos);
					ImGui::DockBuilderSetNodeSize(*m_PendingDockId, target.Size);
					ImGui::DockBuilderDockWindow(m_TabTargetTitle->c_str(), *m_PendingDockId);
					ImGui::DockBuilderFinish(*m_PendingDockId);
				}
			}
			if (m_PendingDockId == 0)
				m_PendingDockId.reset();
			m_TabTargetTitle.reset();
		}
		if (m_PendingDockId)
		{
			ImGui::SetNextWindowDockID(*m_PendingDockId, *m_PendingDockId == 0 ? ImGuiCond_Always : ImGuiCond_Once);
			m_PendingDockId.reset();
		}
		if (m_FocusRequested)
		{
			ImGui::SetNextWindowFocus();
			m_FocusRequested = false;
		}
		bool stillOpen = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &stillOpen);
		m_DockId = ImGui::GetWindowDockID();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_FocusOrder = NextFocusOrder();
		if (!stillOpen)
			Close();
		if (!draw || !stillOpen)
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
		if (ImGui::SmallButton("Odłącz"))
			m_PendingDockId = 0;
		ImGui::SameLine();
		ImGui::TextDisabled("%s", m_Session->m_Directory.Utf8().c_str());
		const float topSpace = std::max(1.0f, ImGui::GetContentRegionAvail().y -
			6.0f - 2.0f * ImGui::GetStyle().ItemSpacing.y);
		float topHeight = topSpace * m_TopFraction;
		if (ImGui::BeginChild("##Summary", ImVec2(0.0f, topHeight), ImGuiChildFlags_Borders))
		{
			if (ImGui::BeginChild("##Chain", ImVec2(0.0f,
				std::max(1.0f, std::min(220.0f, 0.35f * ImGui::GetContentRegionAvail().y))), ImGuiChildFlags_Borders))
				renderChain();
			ImGui::EndChild();
			if (!m_Session->m_Error.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Session->m_Error.c_str());
			if (m_Session->m_DetailRun >= 0 && m_Session->m_DetailRun < static_cast<int>(m_Session->m_Chain.size()))
			{
				const auto &run = m_Session->m_Chain[m_Session->m_DetailRun];
				ImGui::SeparatorText((run.directory.filename().Utf8() + " / " + run.outcarName).c_str());
				if (run.data)
					renderSummary(*run.data);
				else
					ImGui::TextDisabled("%s", run.job ? "Loading..." : run.error.c_str());
			}
		}
		ImGui::EndChild();
		PlotHeightHandle("##TopBottom", topHeight, 1.0f);
		m_TopFraction = std::clamp(topHeight / topSpace, 0.15f, 0.8f);

		const ImVec2 bottom = ImGui::GetContentRegionAvail();
		const float splitWidth = 6.0f;
		const float width = std::max(1.0f, bottom.x - splitWidth - 2.0f * ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::BeginChild("##Convergence", ImVec2(width * m_LeftFraction, std::max(1.0f, bottom.y)), ImGuiChildFlags_Borders))
			renderPlots(true);
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::Button("##LeftRight", ImVec2(splitWidth, std::max(1.0f, bottom.y)));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActive())
			m_LeftFraction = std::clamp(m_LeftFraction + ImGui::GetIO().MouseDelta.x / width, 0.15f, 0.85f);
		ImGui::SameLine();
		if (ImGui::BeginChild("##KsLevels", ImVec2(0.0f, std::max(1.0f, bottom.y)), ImGuiChildFlags_Borders))
			renderPlots(false);
		ImGui::EndChild();
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
	}
} // namespace DefectStudio
