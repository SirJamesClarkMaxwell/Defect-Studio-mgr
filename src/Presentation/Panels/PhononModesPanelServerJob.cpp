#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <imgui.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"

#if defined(DS_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#undef GetJob // winspool.h macro would rename JobSystem::GetJob
#endif

namespace DefectStudio
{
	void PhononModesPanel::drawServerJob()
	{
		if (!ImGui::CollapsingHeader("Serwer")) return;
		auto &controls = m_Session->ServerJobControls();
		const auto request = m_Session->ServerJobInputs();
		ImGui::TextDisabled("Przygotuj zadanie na serwer. Metoda HR, cutoff i widmo: sekcja Wejścia.");
		ImGui::SetItemTooltip("Zmierzony szczyt RAM: 10.6 GB dla n=10, 30 GB dla n=50, 36 rang MPI. Bez modelu predykcji.");
		ImGui::BeginDisabled(m_Session->IsBusy());
		{
			PropertyGrid grid("##serverJob", PropertyGridLabelWidth({"n_min", "n_max", "Szablony", "Folder zadania"}));
			if (grid)
			{
				grid.Row("n_min"); ImGui::InputInt("##serverNMin", &controls.nMin);
				grid.Row("n_max"); ImGui::InputInt("##serverNMax", &controls.nMax);
				grid.Row("Szablony"); (void)DrawFolder("serverTemplates", controls.templates, controls.message, true, request.templatesDirectory);
				grid.Row("Folder zadania"); (void)DrawFolder("serverOutput", controls.output, controls.message, true, request.inputs.outputDirectory);
			}
		}
		const auto blocker = m_Session->ServerJobBlocker();
		ImGui::BeginDisabled(!blocker.empty());
		if (ImGui::Button("Przygotuj...")) m_Session->SubmitServerJob();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::TextWrapped("%s", blocker.c_str());
		if (!controls.message.empty()) ImGui::TextWrapped("%s", controls.message.c_str());
		if (!controls.result.Empty())
		{
			ImGui::TextWrapped("%s", PhononPanelSession::DirectoryLabel(controls.result,
				m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots).c_str());
			if (ImGui::Button("Otwórz folder zadania"))
			{
#if defined(DS_PLATFORM_WINDOWS)
				const auto result = ShellExecuteW(nullptr, L"open", controls.result.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				if (reinterpret_cast<std::intptr_t>(result) <= 32) controls.message = "Nie udało się otworzyć folderu zadania.";
#else
				Platform::ProcessRunOptions options;
#if defined(DS_PLATFORM_MACOS)
				options.executable = Path("/usr/bin/open");
#else
				options.executable = Path("/usr/bin/xdg-open");
#endif
				options.arguments = {controls.result.Utf8()};
				const auto result = Platform::RunProcess(options);
				if (!result) controls.message = result.Error().userMessage;
#endif
			}
		}
		if (m_Session->m_ServerJob)
		{
			const auto jobs = m_Session->m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_Session->m_ServerJobId) : std::nullopt;
			if (snapshot) ImGui::ProgressBar(snapshot->totalWork > 0 ? snapshot->completedWork / snapshot->totalWork : 0,
				ImVec2(-1, 0), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj przygotowanie")) m_Session->cancelJob(m_Session->m_ServerJobId);
		}
	}
}
