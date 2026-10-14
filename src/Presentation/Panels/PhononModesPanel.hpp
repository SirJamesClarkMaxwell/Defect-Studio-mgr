#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/PhononPanelSettingsIO.hpp"
#include "IO/PhononDispersionIO.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

namespace DefectStudio
{
	// Linked inputs, modes and plots over one session; only widget state belongs to a view.
	class PhononModesPanel final : public IPanel
	{
	public:
		enum class View { Main, Modes, Plots };
		PhononModesPanel(Ref<PhononPanelSession> session, View view, std::string title, bool visibleByDefault = false);
		[[nodiscard]] Ref<PhononPanelSession> Session() const noexcept { return m_Session; }
		[[nodiscard]] View GetView() const noexcept { return m_View; }

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] const char *GetSubcategory() const override { return "Fonony"; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// EditorLayer on project open (and with defaults on project close). Stops any animation
		// first, then loads settings.resultDirectory if it is set. Does not call the save handler.
		void SetPersistedState(PhononPanelSettings settings, Path projectDirectory);
		void SetProjectRoots(std::vector<ProjectRootEntry> roots);
		// Called with the settings after every change worth keeping; EditorLayer writes phonon_panel.yaml.
		void SetSaveHandler(std::function<void(const PhononPanelSettings &)> handler) { m_Session->SetSaveHandler(std::move(handler)); }

		// Project Tree RMB "Fonony -> ustaw jako ..." on a folder. role: "fc" | "gs" | "es" | "triplet" |
		// "result" ("result" loads it). Shows the panel and saves.
		void SetDirectory(const std::string &role, const Path &directory);

		// Writes the rest positions back if an animation is running. EditorLayer calls this before
		// any project/structure save and on project change.
		void StopAnimation();

	private:

		void selectMode(int index);

		void drawInputs();
		bool DrawFolder(const char *id, Path &path, std::string &error, bool editable = true, const Path &hint = {});
		void drawDatasets();

		void drawZplInputs();

		void drawDispersion();

		void drawEmbedding();
		void drawCcd();

		void drawCcdPlotTabs(); // inside the shared plot tab bar; also available without phonons
		void drawCcdPlot(const CcdAnalysisResult &result);
		void drawCcdPlPlot(const CcdAnalysisResult &result);

		void drawSummary();
		void drawModesTable();
		void drawPlots();
		void drawComparison();
		void drawVisualisation();

		void persist();
		void drawDispersionPlot();
		void drawFigure(const PlotData &plot, const std::string &label);
		struct FigureState
		{
			std::array<char, 512> name{};
			float widthCm = 8.5f, heightCm = 6.5f;
			bool pdf = true, svg = false, png = false;
		};
		std::unordered_map<std::string, FigureState> m_Figures;
		Path m_FigureProject;

		Ref<PhononPanelSession> m_Session;
		View m_View;
		std::unordered_map<std::string, std::pair<std::vector<char>, bool>> m_FolderEdits;
		int m_ZplPreset = 3;
		std::optional<std::string> m_ZplPresetFormula;
		std::uint64_t m_LastPlotRevision = 0;
		int m_PlotTab = -1;
		bool m_ScrollToMode = false;
		int m_LastSelectedMode = -1;
		std::unordered_map<std::string, PlotExportState> m_PlotExports;
		PlotExportState m_TableExport;
		PlotExportState m_ComparisonExport{true};
		int m_ModeSortColumn = 1;
		bool m_ModeSortAscending = true;
	};
} // namespace DefectStudio
