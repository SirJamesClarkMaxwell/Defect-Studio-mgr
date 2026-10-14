#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "Core/Platform/ProcessRunner.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "Presentation/Figures/FigureSpec.hpp"

#if defined(DS_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

namespace DefectStudio
{
	namespace
	{
		constexpr std::string_view ThesisStyle =
			"figure.facecolor: white\naxes.facecolor: white\nfont.size: 8\naxes.labelsize: 8\naxes.titlesize: 8\n"
			"legend.fontsize: 8\nxtick.labelsize: 8\nytick.labelsize: 8\naxes.linewidth: 0.8\n"
			"xtick.direction: in\nytick.direction: in\ntext.usetex: False\nmathtext.fontset: dejavusans\n";

		bool SaveSpec(const Path &project, const FigureSpec &spec, Path &path, std::string &error)
		{
			try
			{
				const auto json = SerializeFigureSpec(spec);
				FigureSpec checked;
				if (!ParseFigureSpec(json, checked, error)) return false;
				path = project.Join("figures") / Path::FromUtf8(spec.name + ".figure.json");
				const auto stylePath = path.parent_path() / Path::FromUtf8(spec.style + ".mplstyle");
				if (!FileSystem::Exists(stylePath.Native()) && !TextFileIO::Save(stylePath, ThesisStyle, error)) return false;
				const auto temporary = path.parent_path() / Path::FromUtf8(path.filename().Utf8() + ".tmp");
				if (!TextFileIO::Save(temporary, json, error))
				{
					(void)FileSystem::Remove(temporary.Native());
					return false;
				}
				std::error_code renameError;
				const bool saved = FileSystem::Rename(temporary.Native(), path.Native(), renameError);
				if (!saved)
				{
					error = renameError.message();
					(void)FileSystem::Remove(temporary.Native());
				}
				return saved;
			}
			catch (const std::exception &exception)
			{
				error = exception.what();
				return false;
			}
		}

		void OpenFiguresFolder(const Path &directory, std::string &message)
		{
			std::error_code error;
			FileSystem::CreateDirectories(directory.Native(), error);
			if (error) { message = error.message(); return; }
#if defined(DS_PLATFORM_WINDOWS)
			const auto result = ShellExecuteW(nullptr, L"open", directory.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			if (reinterpret_cast<std::intptr_t>(result) <= 32) message = "Nie udało się otworzyć folderu figures.";
#else
			Platform::ProcessRunOptions options;
#if defined(DS_PLATFORM_MACOS)
			options.executable = Path("/usr/bin/open");
#else
			options.executable = Path("/usr/bin/xdg-open");
#endif
			options.arguments = {directory.Utf8()};
			const auto result = Platform::RunProcess(options);
			if (!result) message = result.Error().userMessage;
			else if (result->exitCode != 0) message = "Nie udało się otworzyć folderu figures.";
#endif
		}
	}

	void PhononModesPanel::drawFigure(const PlotData &plot, const std::string &label)
	{
		const auto &project = m_Session->m_ProjectDirectory;
		if (m_FigureProject != project)
		{
			m_Figures.clear();
			m_FigureProject = project;
		}
		ImGui::SameLine();
		ImGui::PushID(plot.id.c_str());
		auto &state = m_Figures[plot.id];
		const bool unavailable = project.Empty();
		const bool running = m_Session->m_FigureJobId != 0;
		const bool noData = plot.series.empty();
		ImGui::BeginDisabled(unavailable || running || noData);
		if (ImGui::SmallButton("Rycina…"))
		{
			if (state.name[0] == '\0')
			{
				std::string name = plot.id + "_" + label;
				for (auto &ch : name)
					if (static_cast<unsigned char>(ch) < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) ch = '_';
				while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
				std::copy_n(name.begin(), std::min(name.size(), state.name.size() - 1), state.name.begin());
			}
			ImGui::OpenPopup("##figure");
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", unavailable ? "Otwórz projekt, aby zapisać rycinę w jego folderze figures." :
			running ? "Trwa renderowanie rycin." : noData ? "Brak serii do eksportu." : "Zapisz snapshot wykresu i renderuj przez matplotlib.");
		ImGui::SameLine();
		ImGui::BeginDisabled(unavailable || running);
		if (ImGui::SmallButton("Renderuj wszystkie")) m_Session->submitFigure({}, true);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", unavailable ? "Otwórz projekt, aby renderować ryciny." :
			running ? "Trwa renderowanie rycin." : "Renderuje ponownie wszystkie figures/*.figure.json z bieżącym plikiem stylu.");
		if (unavailable) ImGui::TextDisabled("Otwórz projekt, aby zapisać ryciny w folderze figures.");
		if (ImGui::BeginPopup("##figure"))
		{
			{
				PropertyGrid grid("##figureSize", PropertyGridLabelWidth({"Nazwa", "Szerokość [cm]", "Wysokość [cm]"}));
				if (grid)
				{
					grid.Row("Nazwa");
					ImGui::InputText("##name", state.name.data(), state.name.size());
					ImGui::SetItemTooltip("Nazwa pliku w folderze figures projektu (bez rozszerzenia).");
					grid.Row("Szerokość [cm]");
					ImGui::DragFloat("##width", &state.widthCm, 0.1f, 1.0f, 100.0f, "%.2f");
					ImGui::SetItemTooltip("Fizyczna szerokość ryciny; 8.5 cm = jedna kolumna, 16 cm = cała strona.");
					grid.Row("Wysokość [cm]");
					ImGui::DragFloat("##height", &state.heightCm, 0.1f, 1.0f, 100.0f, "%.2f");
					ImGui::SetItemTooltip("Fizyczna wysokość ryciny.");
				}
			}
			if (ImGui::Button("8.5 cm")) state.widthCm = 8.5f;
			ImGui::SameLine();
			if (ImGui::Button("16 cm")) state.widthCm = 16.0f;
			ImGui::Checkbox("PDF", &state.pdf); ImGui::SameLine();
			ImGui::Checkbox("SVG", &state.svg); ImGui::SameLine();
			ImGui::Checkbox("PNG", &state.png);
			if (plot.id == "CCD") ImGui::TextWrapped("Rycina CCD nie zawiera jeszcze strzałek, klamer i adnotacji.");
			const bool noFormat = !state.pdf && !state.svg && !state.png;
			ImGui::BeginDisabled(unavailable || running || noData || noFormat || m_Session->m_JobSystem.expired());
			if (ImGui::Button("Zapisz i renderuj"))
			{
				FigureSpec spec;
				spec.name = state.name.data();
				spec.widthCm = state.widthCm; spec.heightCm = state.heightCm;
				spec.formats.clear();
				if (state.pdf) spec.formats.push_back("pdf");
				if (state.svg) spec.formats.push_back("svg");
				if (state.png) spec.formats.push_back("png");
				spec.plot = plot;
				spec.createdBy = "DefectStudio 0.1.0";
				Path path;
				if (SaveSpec(project, spec, path, m_Session->m_FigureMessage)) m_Session->submitFigure(path, false);
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("%s", noFormat ? "Wybierz co najmniej jeden format." :
				m_Session->m_JobSystem.expired() ? "JobSystem niedostępny." : "Zapisz dane i ustawienia, a następnie renderuj w tle.");
			ImGui::BeginDisabled(unavailable);
			if (ImGui::Button("Otwórz folder figures")) OpenFiguresFolder(project.Join("figures"), m_Session->m_FigureMessage);
			ImGui::EndDisabled();
			if (!m_Session->m_FigureMessage.empty()) ImGui::TextWrapped("%s", m_Session->m_FigureMessage.c_str());
			ImGui::EndPopup();
		}
		if (!m_Session->m_FigureMessage.empty()) ImGui::TextWrapped("%s", m_Session->m_FigureMessage.c_str());
		ImGui::PopID();
	}
}
