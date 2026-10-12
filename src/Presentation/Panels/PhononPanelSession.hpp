#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Vibrations/PhononModes.hpp"
#include "IO/PhononPanelSettingsIO.hpp"
#include "IO/ProjectRootsIO.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

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
		// Same lexical containment rule as StoredPath: project first, then a labelled tree root.
		[[nodiscard]] static std::string DirectoryLabel(const Path &path, const Path &projectDirectory,
			const std::vector<ProjectRootEntry> &roots);
		// Pasted spaces/quotes are stripped; relative input is resolved against the project.
		[[nodiscard]] static Path ResolveDirectoryText(std::string_view text, const Path &projectDirectory);
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
		// (empty = all), in ascending frequency order.
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

		// Rebuilds window.modeArrows from the selected mode and Settings() (threshold, arrow length,
		// colours, showArrows). Clears it (nullopt) when there is no selection, showArrows is false or
		// the window does not match. Returns false (outError set) only for a mismatch.
		bool ApplyArrows(RendererWindowState &window, std::string &outError) const;

		// Animation. Start captures the rest positions (no-op if already animating or no valid
		// selection / mismatching window). Advance moves the phase by 2*pi*dt/period and writes the
		// frame; SetPhase writes the frame for a given phase (the paused slider). Stop writes the rest
		// positions back and forgets them. Advance/SetPhase do nothing unless animating.
		void StartAnimation(RendererWindowState &window);
		void AdvanceAnimation(RendererWindowState &window, double deltaSeconds);
		void SetPhase(RendererWindowState &window, double phaseRadians);
		void StopAnimation(RendererWindowState &window);
		[[nodiscard]] bool IsAnimating() const noexcept { return m_RestPositions.has_value(); }
		[[nodiscard]] bool RestPositionsCaptured() const noexcept { return m_RestPositions.has_value(); }
		[[nodiscard]] double Phase() const noexcept { return m_Phase; }

	private:
		PhononPanelSettings m_Settings;
		std::optional<PhononAnalysisResult> m_Result;
		std::optional<std::vector<glm::vec3>> m_RestPositions;
		double m_Phase = 0.0;
	};
} // namespace DefectStudio
