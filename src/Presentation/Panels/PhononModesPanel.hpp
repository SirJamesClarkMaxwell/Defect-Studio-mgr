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

namespace DefectStudio
{
	class EventBus;
	class JobSystem;
	class RendererLayer;
	class PhononAnalysisJob;
	class CcdInputsJob;
	class CcdAnalysisJob;
	class PhononDispersionJob;

	// task/104d: Gamma phonon modes of a defect supercell and the harmonic PL built on them
	// (docs/work/project/tasks/104d-phonon-panel.md). Computes through PhononAnalysisJob or loads an
	// existing result directory, shows the mode table and plots, and draws the selected mode as
	// arrows / animation into the renderer window that holds the force-constant supercell.
	class PhononModesPanel final : public IPanel
	{
	public:
		explicit PhononModesPanel(RendererLayer &rendererLayer, WeakRef<JobSystem> jobSystem, Ref<EventBus> eventBus,
			std::string title = "Phonons", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] const char *GetSubcategory() const override { return "Fonony"; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// EditorLayer on project open (and with defaults on project close). Stops any animation
		// first, then loads settings.resultDirectory if it is set. Does not call the save handler.
		void SetPersistedState(PhononPanelSettings settings, Path projectDirectory);
		void SetProjectRoots(std::vector<ProjectRootEntry> roots);
		// Called with the settings after every change worth keeping; EditorLayer writes phonon_panel.yaml.
		void SetSaveHandler(std::function<void(const PhononPanelSettings &)> handler) { m_SaveHandler = std::move(handler); }

		// Project Tree RMB "Fonony -> ustaw jako ..." on a folder. role: "fc" | "gs" | "es" | "triplet" |
		// "result" ("result" loads it). Shows the panel and saves.
		void SetDirectory(const std::string &role, const Path &directory);

		// Writes the rest positions back if an animation is running. EditorLayer calls this before
		// any project/structure save and on project change.
		void StopAnimation();

	private:
		[[nodiscard]] std::optional<std::reference_wrapper<RendererWindowState>> targetWindow();
		void clearWindow();
		void selectMode(int index);
		void cancelJob();
		void drawInputs();
		bool DrawFolder(const char *id, Path &path, std::string &error, bool editable = true, const Path &hint = {});
		void drawDatasets();
		void scanDatasets();
		void drawZplInputs();
		void drawDatasetSpectra(bool pl, double minimum, double maximum); // x range: samples outside are not drawn
		[[nodiscard]] Path computationOutputPath() const;
		[[nodiscard]] Path defaultDatasetPath(const std::vector<Path> &inputs, const std::string &kind) const;
		void drawDispersion();
		void submitDispersion();
		void pollDispersionJob();
		bool loadDispersion(const Path &directory);
		[[nodiscard]] Path dispersionOutputPath() const;
		void drawEmbedding();
		void drawCcd();
		void submitCcd();
		void pollCcdJob();
		[[nodiscard]] Path ccdOutputPath() const;
		[[nodiscard]] Path ccdAnalysisOutputPath() const;
		[[nodiscard]] std::string ccdAnalysisBlocker() const;
		void submitCcdAnalysis();
		void pollCcdAnalysisJob();
		bool loadCcdResult(const Path &directory);
		void drawCcdPlotTabs(); // inside the shared plot tab bar; also available without phonons
		void drawCcdPlot(const CcdAnalysisResult &result);
		void drawCcdPlPlot(const CcdAnalysisResult &result);
		void loadEmbeddingSummary(const Path &directory);
		void drawSummary();
		void drawModesTable();
		void drawPlots();
		void drawVisualisation();
		void submitComputation();
		void pollJob();
		void loadResult(const Path &directory);
		[[nodiscard]] Path supercellPath() const;
		void openSupercellWindow();
		void syncWindow(); // finds the bound window, applies arrows, advances the animation
		void persist();

		RendererLayer &m_RendererLayer;
		WeakRef<JobSystem> m_JobSystem;
		Ref<EventBus> m_EventBus;
		std::function<void(const PhononPanelSettings &)> m_SaveHandler;

		Ref<PhononPanelSession> m_Session = CreateRef<PhononPanelSession>();
		Path m_ProjectDirectory;
		std::vector<ProjectRootEntry> m_ProjectRoots;
		// Keep uncommitted directory text intact until the edit ends.
		std::unordered_map<std::string, std::pair<std::vector<char>, bool>> m_FolderEdits;
		mutable std::unordered_map<std::string, Path> m_DefaultDatasetDirectories;
		int m_ZplPreset = 3;
		// The renderer window holding the force-constant supercell, found by
		// structure.sourcePath == <fc dir>/POSCAR after openSupercellWindow(); empty = not bound.
		std::string m_WindowId;
		std::vector<std::string> m_WindowsBeforeOpen;
		Ref<PhononAnalysisJob> m_PendingJob;
		JobId m_PendingJobId = 0;
		Ref<CcdInputsJob> m_PendingCcdJob;
		JobId m_PendingCcdJobId = 0;
		Ref<CcdAnalysisJob> m_PendingCcdAnalysisJob;
		JobId m_PendingCcdAnalysisJobId = 0;
		Ref<PhononDispersionJob> m_PendingDispersionJob;
		JobId m_PendingDispersionJobId = 0;
		std::optional<PhononDispersion> m_Dispersion;
		std::string m_DispersionError;
		bool m_ResetDispersionPlot = true;
		bool m_ResetPlots = true;
		int m_PlotTab = -1;
		bool m_ScrollToMode = false;
		std::array<int, 3> m_EmbeddingMult = {12, 12, 1};
		std::string m_EmbeddingSummary;
		std::string m_CcdResult;
		std::string m_CcdError;
		std::string m_CcdAnalysisError;
		std::string m_Error;
		std::string m_WindowError;
		bool m_BindRequested = false;
		bool m_Paused = false;
	};
} // namespace DefectStudio
