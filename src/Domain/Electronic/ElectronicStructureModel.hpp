#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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

	// Real-space grid of one Kohn-Sham orbital's wavefunction (from WAVECAR via puntukas), the raw
	// input to isosurface mesh generation (T14-core). values.size() == dimensions.x*y*z, C-order
	// (x slowest, z fastest). Signed (real part of the wavefunction, not |psi|^2) so +/- lobes can
	// be colored independently.
	struct OrbitalGridData
	{
		glm::ivec3 dimensions = glm::ivec3(0);
		glm::mat3 cell = glm::mat3(1.0f); // direct real-space cell (Angstrom), cell[i] = lattice vector i
		std::vector<float> values;
		float energy = 0.0f;
		float occupation = 0.0f;
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
	// per spin channel across the given (already band-windowed) records. If only one channel is
	// populated at all (non-spin-polarized calculation, or puntukas mirroring one channel into
	// the other) that's still closed-shell -> Singlet. Otherwise equal sums -> paired spins
	// (Singlet); unequal -> net spin polarization within the window (Triplet-like, an unpaired
	// electron pair across the two channels). Both channels empty -> Unknown.
	[[nodiscard]] SpinMultiplicity ClassifySpinMultiplicity(
		const std::vector<OrbitalRecord> &orbitalsInWindow);
} // namespace DefectStudio
