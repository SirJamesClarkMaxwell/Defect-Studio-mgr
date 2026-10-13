#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Vibrations/PhononModes.hpp"
#include "Domain/Vibrations/CcdAnalysis.hpp"
#include "IO/PhononPanelSettingsIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/AnalysisDatasetIO.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	struct RendererWindowState;
	class JobSystem;
	class AnalysisDatasetScanJob;

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
		~PhononPanelSession();
		void SetDatasets(std::vector<AnalysisDatasetRef> datasets);
		void AddDataset(AnalysisDatasetRef dataset);
		[[nodiscard]] const std::vector<AnalysisDatasetRef> &Datasets() const noexcept { return m_Datasets; }
		void SetCompared(const Path &directory, bool compared);
		[[nodiscard]] bool IsCompared(const Path &directory) const;
		// Selection only; the panel commits it after loading the requested result successfully.
		bool SelectDataset(const Path &directory);
		[[nodiscard]] std::optional<std::reference_wrapper<const PhononAnalysisResult>> Spectra(
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
		std::vector<AnalysisDatasetRef> m_Datasets;
		std::map<std::string, std::pair<std::optional<PhononAnalysisResult>, std::string>> m_Spectra;
		std::map<std::string, std::pair<std::optional<CcdAnalysisResult>, std::string>> m_CcdResults;
		std::optional<CcdAnalysisResult> m_CcdResult;
		WeakRef<JobSystem> m_ScanJobs;
		Ref<AnalysisDatasetScanJob> m_ScanJob;
		JobId m_ScanJobId = 0;
		std::string m_ScanMessage;
		PhononPanelSettings m_Settings;
		std::optional<PhononAnalysisResult> m_Result;
		std::optional<std::vector<glm::vec3>> m_RestPositions;
		double m_Phase = 0.0;
	};
} // namespace DefectStudio
