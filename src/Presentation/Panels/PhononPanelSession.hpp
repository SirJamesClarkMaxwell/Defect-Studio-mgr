#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <deque>
#include <variant>

#include <glm/glm.hpp>

#include "Domain/Vibrations/PhononModes.hpp"
#include "Domain/Vibrations/CcdAnalysis.hpp"
#include "IO/PhononPanelSettingsIO.hpp"
#include "IO/PhononDispersionIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/AnalysisDatasetIO.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"

namespace DefectStudio
{
	struct RendererWindowState;
	class EventBus;
	class JobSystem;
	class PhononModesPanel;
	class PhononAnalysisJob;
	class CcdInputsJob;
	class CcdAnalysisJob;
	class PhononDispersionJob;
	class AnalysisDatasetScanJob;
	namespace Tests { struct PhononBatchTestAccess; }

	enum class PhononBatchStep { Phonons, CcdAnalysis };
	enum class PhononCaseStatus { Idle, Queued, Running, Done, Failed, Cancelled };
	struct PhononCaseRun
	{
		PhononCaseStatus status = PhononCaseStatus::Idle;
		std::string message;
		Path output;
	};

	// task/104d: everything the Phonons panel does that is not ImGui - the loaded result, the
	// persisted settings, the mode list, and what goes into the bound renderer window (mode arrows
	// and animated atom positions). The panel only draws widgets and forwards intents here.
	//
	// Animation writes RendererWindowState::structure.atoms[i].cartesianPosition directly (the
	// same path an ImGuizmo drag uses; the backend's position hash redraws atoms and bonds). The
	// rest positions are captured at StartAnimation and written back by StopAnimation - which the
	// panel must call on: stop button, mode change, result change, panel hidden, window closed,
	// project change and before any project/structure save. RestPositionsCaptured() lets the panel
	// assert that.
	class PhononPanelSession final
	{
	public:
		PhononPanelSession();
		explicit PhononPanelSession(WeakRef<JobSystem> jobs, Ref<EventBus> eventBus = {});
		~PhononPanelSession();
		void Update(std::vector<RendererWindowState> &windows, double deltaSeconds, bool anyViewVisible);
		void Changed();
		void FlushChanges();
		[[nodiscard]] std::uint64_t PlotRevision() const noexcept { return m_PlotRevision; }
		void InvalidatePlots() noexcept { ++m_PlotRevision; }
		void SetPlZpl(std::string choice);
		void SubmitComputation();
		void SubmitCcdInputs();
		void SubmitCcdAnalysis();
		void SubmitDispersion();
		void StartBatch(std::vector<std::string> caseIds, std::vector<PhononBatchStep> steps);
		void CancelBatch();
		[[nodiscard]] const PhononCaseRun &CaseRun(const std::string &caseId) const;
		[[nodiscard]] bool IsBatchRunning() const;
		[[nodiscard]] std::pair<std::size_t, std::size_t> BatchProgress() const;
		[[nodiscard]] std::string ComputationBlocker() const;
		[[nodiscard]] std::string ComputationBlocker(const PhononCaseSettings &settings, const std::string &caseId) const;
		[[nodiscard]] Path CaseOutputDirectory(const std::string &kindDirName,
			const PhononCaseSettings &settings, const std::string &caseId) const;
		[[nodiscard]] bool IsBusy() const noexcept
		{
			return IsBatchRunning() || m_PendingJob || m_PendingCcdJob || m_PendingCcdAnalysisJob || m_PendingDispersionJob;
		}
		void SetPersistedState(PhononPanelSettings settings, Path projectDirectory);
		void SetProjectRoots(std::vector<ProjectRootEntry> roots);
		void SetDirectory(const std::string &role, const Path &directory);
		[[nodiscard]] const std::vector<PhononCase> &Cases() const;
		[[nodiscard]] const std::string &ActiveCaseId() const;
		std::string AddCase(std::string name);
		std::string DuplicateCase(const std::string &id);
		void RemoveCase(const std::string &id);
		void RenameCase(const std::string &id, std::string name);
		bool ActivateCase(const std::string &id);
		void AttachDataset(const std::string &caseId, const Path &directory);
		void DetachDataset(const std::string &caseId, const Path &directory);
		[[nodiscard]] std::vector<Path> ProposedDatasets(const std::string &caseId) const;
		std::vector<std::string> CreateCasesFromDatasets();
		[[nodiscard]] Path CaseOutputDirectory(const std::string &kindDirName) const;
		[[nodiscard]] std::string CaseWindowTitle() const;
		[[nodiscard]] bool CaseChecked(const std::string &id) const;
		void SetCaseChecked(const std::string &id, bool checked);
		[[nodiscard]] std::optional<std::reference_wrapper<const PhononAnalysisResult>> CachedCaseResult(const std::string &id) const;
		[[nodiscard]] bool HasUnattachedCaseDatasets() const;
		[[nodiscard]] std::string DatasetCaseId(const Path &directory) const;
		void StopAnimation();
		[[nodiscard]] const std::string &WindowId() const noexcept { return m_WindowId; }
		void SetSaveHandler(std::function<void(const PhononPanelSettings &)> handler) { m_SaveHandler = std::move(handler); }
		void SetShowViewsHandler(std::function<void(bool)> handler) { m_ShowViewsHandler = std::move(handler); }
		void ShowViews(bool visible) { if (m_ShowViewsHandler) m_ShowViewsHandler(visible); }
		void SetDatasets(std::vector<AnalysisDatasetRef> datasets);
		void AddDataset(AnalysisDatasetRef dataset);
		[[nodiscard]] const std::vector<AnalysisDatasetRef> &Datasets() const noexcept { return m_Datasets; }
		void SetCompared(const Path &directory, bool compared);
		[[nodiscard]] bool IsCompared(const Path &directory) const;
		// Selection only; the panel commits it after loading the requested result successfully.
		// Also makes it the current dataset: the one highlighted row, whatever its kind.
		bool SelectDataset(const Path &directory);
		[[nodiscard]] const Path &CurrentDataset() const noexcept { return m_CurrentDataset; }
		void SetCurrentDataset(Path directory) { m_CurrentDataset = std::move(directory); }
		[[nodiscard]] std::optional<std::reference_wrapper<const PhononAnalysisResult>> Spectra(
			const Path &directory, std::string &outError);
		[[nodiscard]] std::optional<std::reference_wrapper<const PhononAnalysisResult>> PlotModes(
			const Path &directory, std::string &outError);
		[[nodiscard]] std::optional<std::reference_wrapper<const CcdAnalysisResult>> Ccd(
			const Path &directory, std::string &outError);
		void SetCcdResult(CcdAnalysisResult result);
		void ClearCcdResult();
		[[nodiscard]] std::optional<std::reference_wrapper<const CcdAnalysisResult>> CcdResult() const noexcept;
		[[nodiscard]] static std::pair<double, double> UsefulRange(const CcdAnalysisResult &result);
		// Classical crossings around the branch minimum; absent if the sampled well does not bracket the level.
		[[nodiscard]] static std::optional<std::pair<double, double>> CcdLevelRange(
			const CcdAnalysisResult &result, bool excited, std::size_t n);
		void StartDatasetScan(WeakRef<JobSystem> jobs, std::vector<Path> roots);
		void CancelDatasetScan();
		bool PollDatasetScan(); // commits completed job on main thread, true if the list changed
		[[nodiscard]] bool IsScanningDatasets() const noexcept { return m_ScanJob != nullptr; }
		[[nodiscard]] const std::string &DatasetScanMessage() const noexcept { return m_ScanMessage; }
		[[nodiscard]] static std::string PlCurveChoice(const PhononAnalysisResult &result, std::string_view preferred);
		[[nodiscard]] static const std::vector<double> &PlCurve(const PhononAnalysisResult &result, std::string_view choice);
		// True also on path resolution failure: never enable a destructive output in that case.
		[[nodiscard]] static bool OutputContainsInput(const Path &output, const Path &input);
		// Same lexical containment rule as StoredPath: project first, then a labelled tree root.
		[[nodiscard]] static std::string DirectoryLabel(const Path &path, const Path &projectDirectory,
			const std::vector<ProjectRootEntry> &roots);
		// Pasted spaces/quotes are stripped; relative input is resolved against the project.
		[[nodiscard]] static Path ResolveDirectoryText(std::string_view text, const Path &projectDirectory);
		// Relative paths use the project directory; empty paths never match. Windows ignores case.
		[[nodiscard]] static bool SameFilePath(const Path &left, const Path &right, const Path &projectDirectory);
		// Sorted grid inside [minimum, maximum], with one sample on either side, clamped to the grid.
		[[nodiscard]] static std::pair<std::size_t, std::size_t> GridIndexRange(std::span<const double> grid,
			double minimum, double maximum);
		// Default x range of one result: modes (or where S(hw) > 1e-3 max) for S, PL rise .. ZPL + 0.15 eV for PL.
		[[nodiscard]] static std::pair<double, double> UsefulRange(const PhononAnalysisResult &result, bool pl, std::string_view plZpl);
		[[nodiscard]] std::pair<double, double> ModeEnergyRangeMeV() const;
		[[nodiscard]] std::pair<double, double> PlEnergyRangeEv() const;

		[[nodiscard]] PhononPanelSettings &Settings() noexcept { return m_Settings; }
		[[nodiscard]] const PhononPanelSettings &Settings() const noexcept { return m_Settings; }

		// Replaces the loaded result; keeps Settings().selectedMode if it is still a valid,
		// non-translation mode, otherwise selects the non-translation mode with the largest S_k
		// (-1 when there is none).
		void SetResult(PhononAnalysisResult result);
		void ClearResult();
		[[nodiscard]] const PhononAnalysisResult *Result() const noexcept;

		// Mode indices for the table: translations left out, Settings().irrepFilter applied
		// (empty = all) and minHuangRhys applied, in ascending frequency order. Selection is retained.
		[[nodiscard]] std::vector<std::size_t> VisibleModes() const;
		// Distinct irreps of the non-translation modes, sorted, for the filter combo.
		[[nodiscard]] std::vector<std::string> Irreps() const;

		// Ignored for an invalid index or a translation.
		void SelectMode(int modeIndex);
		[[nodiscard]] std::optional<std::size_t> SelectedMode() const;

		// Readouts for the selected mode: arrows that pass the participation threshold, and
		// AtomsCarryingFraction(Settings().carriedFraction). 0 without a selection.
		[[nodiscard]] std::size_t ArrowCount() const;
		[[nodiscard]] std::size_t AtomsCarryingSelectedFraction() const;

		// The window shows this result if it has the same atom count and the same element per atom
		// (RendererAtomData::element vs symbols, in order). outError says what differs.
		[[nodiscard]] bool WindowMatches(const RendererWindowState &window, std::string &outError) const;
		// Title of the window the panel opens; a restored window keeps it.
		static constexpr std::string_view SupercellWindowTitle = "Superkomórka (fonony)";
		// The panel drives a window opened from `supercell`, or its own supercell window (title) showing
		// these atoms - restored from a project saved with another result of the same structure.
		[[nodiscard]] bool CanBindWindow(const RendererWindowState &window, const Path &supercell,
			const Path &projectDirectory) const;

		// Rebuilds window.modeArrows from the selected mode and Settings() (threshold, arrow length,
		// colours, showArrows). During animation, arrowsFollowPhase scales vectors by cos(Phase()).
		// Clears it (nullopt) when there is no selection, showArrows is false or
		// the window does not match. Returns false (outError set) only for a mismatch.
		bool ApplyArrows(RendererWindowState &window, std::string &outError) const;

		// Animation. Start captures the rest positions (no-op if already animating or no valid
		// selection / mismatching window). Advance moves the phase by 2*pi*dt/period and writes the
		// frame and arrows; SetPhase writes both for a given phase (the paused slider). Stop writes the rest
		// positions back and forgets them. Advance/SetPhase do nothing unless animating.
		void StartAnimation(RendererWindowState &window);
		void AdvanceAnimation(RendererWindowState &window, double deltaSeconds);
		void SetPhase(RendererWindowState &window, double phaseRadians);
		void StopAnimation(RendererWindowState &window);
		[[nodiscard]] bool IsAnimating() const noexcept { return m_RestPositions.has_value(); }
		[[nodiscard]] bool RestPositionsCaptured() const noexcept { return m_RestPositions.has_value(); }
		[[nodiscard]] double Phase() const noexcept { return m_Phase; }

	private:
		friend class PhononModesPanel;
		friend void DrawPhononCases(PhononPanelSession &session);
		friend struct Tests::PhononBatchTestAccess;
		struct BatchEntry
		{
			std::string caseId;
			std::variant<PhononAnalysisRequest, CcdAnalysisRequest> request;
		};
		[[nodiscard]] PhononAnalysisRequest computationRequest(const PhononCaseSettings &settings, const std::string &caseId) const;
		[[nodiscard]] CcdAnalysisRequest ccdAnalysisRequest(const PhononCaseSettings &settings, const std::string &caseId) const;
		[[nodiscard]] Path computationOutputPath(const PhononCaseSettings &settings, const std::string &caseId) const;
		[[nodiscard]] Path ccdAnalysisOutputPath(const PhononCaseSettings &settings, const std::string &caseId) const;
		[[nodiscard]] std::string ccdAnalysisBlocker(const PhononCaseSettings &settings, const std::string &caseId,
			bool plannedPhonons = false) const;
		void submitComputation(PhononAnalysisRequest request, const std::string &caseId);
		void submitCcdAnalysis(CcdAnalysisRequest request, const std::string &caseId);
		void submitNextBatch();
		void finishCaseJob(PhononBatchStep step, const std::string &caseId, const Path &output, JobStatus status,
			std::string message);
		[[nodiscard]] std::string commitCaseOutput(const std::string &caseId, PhononBatchStep step, const Path &output);
		[[nodiscard]] JobId submitCaseJob(const Ref<IJob> &job);
		std::deque<BatchEntry> m_BatchQueue;
		std::optional<BatchEntry> m_BatchEntry;
		std::map<std::string, PhononCaseRun> m_CaseRuns;
		std::size_t m_BatchDone = 0, m_BatchTotal = 0;
		bool m_BatchCancelling = false;
		bool m_BatchPhonons = true, m_BatchCcdAnalysis = false;
		// Originating case of each job slot (the slots can overlap, so one shared id would mix them up).
		std::string m_PendingCaseId, m_PendingCcdInputsCaseId, m_PendingCcdAnalysisCaseId;
		// Tests replace only execution; requests and main-thread completion use the real session flow.
		std::function<JobId(const Ref<IJob> &)> m_CaseJobSubmitter;
		void loadResult(const Path &directory);
		void cacheActiveResult();
		void scanDatasets();
		[[nodiscard]] Path defaultDatasetPath(const std::vector<Path> &inputs, const std::string &kind) const;
		[[nodiscard]] Path computationOutputPath() const;
		void cancelJob();
		void cancelCaseJobs();
		void cancelJob(JobId id);
		void pollJob();
		[[nodiscard]] Path dispersionOutputPath() const;
		bool loadDispersion(const Path &directory);
		void pollDispersionJob();
		[[nodiscard]] Path ccdOutputPath() const;
		void pollCcdJob();
		[[nodiscard]] Path ccdAnalysisOutputPath() const;
		[[nodiscard]] std::string ccdAnalysisBlocker() const;
		bool loadCcdResult(const Path &directory);
		void pollCcdAnalysisJob();
		[[nodiscard]] std::optional<std::reference_wrapper<RendererWindowState>> targetWindow();
		void clearWindow();
		[[nodiscard]] Path supercellPath() const;
		void openSupercellWindow();
		void syncWindow(double deltaSeconds); // finds the bound window, applies arrows, advances the animation
		void loadEmbeddingSummary(const Path &directory);
		WeakRef<JobSystem> m_JobSystem;
		Ref<EventBus> m_EventBus;
		std::function<void(const PhononPanelSettings &)> m_SaveHandler;
		std::function<void(bool)> m_ShowViewsHandler;
		bool m_Changed = false;
		std::uint64_t m_PlotRevision = 1;
		std::string m_ObservedPlZpl = "computed";
		// The renderer owns this vector for the session's lifetime; only Update installs the binding.
		std::optional<std::reference_wrapper<std::vector<RendererWindowState>>> m_Windows;
		Path m_ProjectDirectory;
		std::vector<ProjectRootEntry> m_ProjectRoots;
		mutable std::unordered_map<std::string, Path> m_DefaultDatasetDirectories;
		mutable std::unordered_map<std::string, Path> m_CaseOutputDirectories;
		std::map<std::string, std::string> m_CaseWindows;
		std::map<std::string, bool> m_CheckedCases;
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
		std::array<int, 3> m_EmbeddingMult = {12, 12, 1};
		std::string m_EmbeddingSummary;
		std::string m_CcdInputsResult;
		std::string m_CcdError;
		std::string m_CcdAnalysisError;
		std::string m_Error;
		std::string m_WindowError;
		bool m_BindRequested = false;
		bool m_Paused = false;

		std::vector<AnalysisDatasetRef> m_Datasets;
		Path m_CurrentDataset;
		struct CachedPhonons
		{
			std::optional<PhononAnalysisResult> result;
			std::string error;
			bool modesRequested = false;
			std::string modesError;
		};
		std::map<std::string, CachedPhonons> m_Spectra;
		std::map<std::string, std::pair<std::optional<CcdAnalysisResult>, std::string>> m_CcdResults;
		std::optional<CcdAnalysisResult> m_CcdResult;
		WeakRef<JobSystem> m_ScanJobs;
		Ref<AnalysisDatasetScanJob> m_ScanJob;
		JobId m_ScanJobId = 0;
		std::string m_ScanMessage;
		mutable PhononPanelSettings m_Settings;
		std::optional<PhononAnalysisResult> m_Result;
		std::optional<std::vector<glm::vec3>> m_RestPositions;
		double m_Phase = 0.0;
	};
} // namespace DefectStudio
