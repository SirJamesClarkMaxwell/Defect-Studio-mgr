#pragma once

#include <optional>
#include <string>
#include <vector>

namespace DefectStudio
{
	struct OrbitalChannelData
	{
		float energy = 0.0f;
		float occupation = 0.0f;
		float localization = 0.0f;
		std::optional<std::string> irrep;
	};

	struct OrbitalRecord
	{
		int band = 0;
		OrbitalChannelData up;
		OrbitalChannelData down;
	};

	struct BandGapData
	{
		float bandgap = 0.0f;
		float homo = 0.0f;
		float lumo = 0.0f;
	};

	struct ElectronicStructureData
	{
		// nullopt if vasprun.xml was missing/unparsable for this calculation - not fatal, callers
		// should degrade gracefully (e.g. hide band-gap-relative UI, keep orbitals if present).
		std::optional<BandGapData> gap;
		// nullopt if WAVECAR was missing - band gap data above may still be available without it.
		std::optional<std::vector<OrbitalRecord>> orbitals;
	};

	struct LocalizationThresholdSettings
	{
		float minLocalization = 0.0f;
	};

	enum class SpinMultiplicity
	{
		Unknown,
		Singlet,
		Triplet
	};

	// Drops orbital records where neither spin channel meets the localization threshold - a record
	// is kept if EITHER channel is localized enough, since one record spans both channels and an
	// open-shell state can be localized in only one of them.
	[[nodiscard]] std::vector<OrbitalRecord> FilterByLocalizationThreshold(
		const std::vector<OrbitalRecord> &orbitals,
		const LocalizationThresholdSettings &settings);

	// First-pass heuristic (NOT yet validated against real defect calculations): sums occupation
	// per spin channel across the given (already band-windowed) records. Equal sums -> paired
	// spins / closed shell (Singlet). Unequal -> net spin polarization within the window
	// (Triplet-like, an unpaired electron pair across the two channels). Empty window -> Unknown.
	[[nodiscard]] SpinMultiplicity ClassifySpinMultiplicity(
		const std::vector<OrbitalRecord> &orbitalsInWindow);
} // namespace DefectStudio
