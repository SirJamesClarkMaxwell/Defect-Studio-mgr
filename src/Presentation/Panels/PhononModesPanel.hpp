#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/PhononPanelSettingsIO.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

namespace DefectStudio
{
	class EventBus;
	class JobSystem;
	class RendererLayer;
	class PhononAnalysisJob;

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
		// The renderer window holding the force-constant supercell, found by
		// structure.sourcePath == <fc dir>/POSCAR after openSupercellWindow(); empty = not bound.
		std::string m_WindowId;
		std::vector<std::string> m_WindowsBeforeOpen;
		Ref<PhononAnalysisJob> m_PendingJob;
		JobId m_PendingJobId = 0;
		std::string m_Error;
		std::string m_WindowError;
		bool m_BindRequested = false;
		bool m_Paused = false;
	};
} // namespace DefectStudio
