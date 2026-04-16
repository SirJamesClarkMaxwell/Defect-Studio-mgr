#include "Core/dspch.hpp"

#include <algorithm>

#include <imgui.h>

#include "App/Application.hpp"
#include "Presentation/Panels/Settings.hpp"

namespace DefectStudio
{
	Settings::Settings(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
	}

	Ref<IPanel> Settings::Clone() const
	{
		return CreateRef<Settings>(*this);
	}

	void Settings::Render()
	{
		auto &application = Application::Get();
		auto &jobSystem = application.GetJobSystem();
		if (!m_SettingsInitialized)
		{
			m_WorkerThreadCount = static_cast<int>(jobSystem.GetThreadCount());
			m_FontScale = application.GetFontScale();
			m_FontScaleStep = application.GetFontScaleStep();
			m_SettingsInitialized = true;
		}

		if (!IsVisible())
			return;

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(920.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		ImGui::BeginChild("SettingsSidebar", ImVec2(190.0f, 0.0f), true);
		renderSidebar();
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("SettingsContent", ImVec2(0.0f, 0.0f), true);
		switch (m_SelectedTabIndex)
		{
		case 0:
			renderSystemTab();
			break;
		case 1:
			renderDisplayTab();
			break;
		default:
			ImGui::TextUnformatted("This section is a placeholder for future settings.");
			break;
		}
		ImGui::EndChild();

		ImGui::End();
	}

	void Settings::renderSidebar()
	{
		const char *tabs[] = {
			"System",
			"Interface",
			"Viewport",
			"Editing",
			"Animation",
			"Input",
			"Save & Load",
			"File Paths",
		};

		for (int i = 0; i < IM_ARRAYSIZE(tabs); ++i)
		{
			const bool selected = (m_SelectedTabIndex == i);
			if (ImGui::Selectable(tabs[i], selected))
				m_SelectedTabIndex = i;
		}
	}

	void Settings::renderSystemTab()
	{
		auto &application = Application::Get();
		auto &jobSystem = application.GetJobSystem();

		ImGui::TextUnformatted("Job system settings");
		if (ImGui::BeginTable("SystemJobSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Worker threads");
			ImGui::TableSetColumnIndex(1);
			ImGui::PushItemWidth(-1.0f);
			ImGui::InputInt("##WorkerThreads", &m_WorkerThreadCount);
			ImGui::PopItemWidth();
			m_WorkerThreadCount = std::max(1, m_WorkerThreadCount);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Reserve one urgent worker");
			ImGui::TableSetColumnIndex(1);
			ImGui::Checkbox("##ReserveUrgent", &m_ReserveUrgentWorker);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Effective workers");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", m_WorkerThreadCount + (m_ReserveUrgentWorker ? 1 : 0));

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Current threads");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%llu", static_cast<unsigned long long>(jobSystem.GetThreadCount()));

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Actions");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Button("Apply"))
			{
				(void)jobSystem.SetThreadCount(static_cast<std::size_t>(m_WorkerThreadCount + (m_ReserveUrgentWorker ? 1 : 0)));
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset to 5"))
			{
				m_WorkerThreadCount = 5;
				(void)jobSystem.SetThreadCount(static_cast<std::size_t>(m_WorkerThreadCount + (m_ReserveUrgentWorker ? 1 : 0)));
			}

			ImGui::EndTable();
		}
		ImGui::Spacing();
		ImGui::TextWrapped("Reducing worker count is asynchronous. Running jobs continue; queued jobs resume on the new pool size.");
	}

	void Settings::renderDisplayTab()
	{
		auto &application = Application::Get();
		ImGui::TextUnformatted("Display settings");
		if (ImGui::BeginTable("DisplaySettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font scale");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::SliderFloat("##FontScale", &m_FontScale, 0.70f, 2.00f, "%.2f"))
				application.SetFontScale(m_FontScale);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font step (Ctrl +/-)");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::SliderFloat("##FontScaleStep", &m_FontScaleStep, 0.01f, 0.50f, "%.2f"))
				application.SetFontScaleStep(m_FontScaleStep);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Shortcut preview");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Ctrl + / Ctrl - adjusts by %.2f", m_FontScaleStep);

			ImGui::EndTable();
		}
		ImGui::Spacing();
		ImGui::TextWrapped("Shortcut behavior is handled globally by EditorLayer so it works regardless of which panel has focus.");
	}
} // namespace DefectStudio
