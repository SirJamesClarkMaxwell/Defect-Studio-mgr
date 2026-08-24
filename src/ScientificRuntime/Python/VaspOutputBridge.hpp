#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct VaspOrbitalChannelData
	{
		float energy = 0.0f;
		float occupation = 0.0f;
		float localization = 0.0f;
		std::optional<std::string> irrep;
	};

	struct VaspOrbitalRecord
	{
		int band = 0;
		VaspOrbitalChannelData up;
		VaspOrbitalChannelData down;
	};

	struct VaspBandGapData
	{
		float bandgap = 0.0f;
		float homo = 0.0f;
		float lumo = 0.0f;
	};

	// Calculation-level summary fields (Calculation Summary panel, docs/work/project/plans/
	// 2026-08-24-calc-tools.md section 5) - each independently optional since a partial/older
	// OUTCAR or vasprun.xml can have some of these and not others; see vasp_output_load.py's
	// _summary_payload for the per-field try/except that produces this.
	struct VaspOutputSummaryData
	{
		// One entry per ionic step (its converged/last SCF energy) - the "how did it converge"
		// trend. Comes from puntukas' private Vasprun._etot, not a public API - see
		// _summary_payload's comment.
		std::optional<std::vector<double>> energyTrend;
		std::optional<double> finalEnergy;
		std::optional<double> cpuTimeSeconds;
		std::optional<double> userTimeSeconds;
		std::optional<double> systemTimeSeconds;
		std::optional<double> elapsedTimeSeconds;
		std::optional<std::array<double, 3>> totalDrift;
		std::optional<double> nelect;
		std::optional<int> ispin;
		std::optional<double> pressureKilobar;
		std::optional<std::array<std::array<double, 3>, 3>> stressTensorKilobar;
		std::optional<std::string> spaceGroupSymbol;
		std::optional<int> spaceGroupNumber;
		// Crystallographic point group, Schoenflies notation (C2v, D3h, ...) with its Hermann-
		// Mauguin equivalent (mm2, -6m2, ...) - distinct from the space group above (space groups
		// include translation/glide/screw symmetry, point groups don't).
		std::optional<std::string> pointGroupSymbol;
		std::optional<std::string> pointGroupSchoenflies;
	};

	struct VaspOutputData
	{
		Path path;
		// nullopt if vasprun.xml is missing/unparsable for this calculation - not fatal, callers
		// should degrade gracefully (e.g. hide band-gap-relative UI, keep orbital data if present).
		std::optional<VaspBandGapData> gap;
		// nullopt if WAVECAR is missing - band gap data above may still be available without it.
		std::optional<std::vector<VaspOrbitalRecord>> orbitals;
		// Set only when orbitals is nullopt because WAVECAR exists but couldn't be read (corrupted/
		// incomplete transfer, seen on network drives) - distinguishes that case from WAVECAR simply
		// being absent, which leaves this nullopt too.
		std::optional<std::string> orbitalsError;
		// Always present (never nullopt) when the load itself succeeded - individual fields inside
		// may still be nullopt, see VaspOutputSummaryData.
		VaspOutputSummaryData summary;
	};

	// Loads band-gap (HOMO/LUMO), per-band orbital data (energy, occupation, localization factor,
	// irrep, both spin channels), and calculation-summary fields (convergence trend, timing,
	// drift, ...) for one VASP calculation directory, via puntukas' VaspOutput (which itself reads
	// OUTCAR/vasprun.xml/WAVECAR as available). Subprocess-only, same rationale as PuntukasBridge:
	// this is a low-frequency user action, not a hot loop.
	class VaspOutputBridge final
	{
	public:
		// includeOrbitals=false skips the WAVECAR read/per-band diagonalization entirely (real
		// per-band cost) - set false for callers (CalculationSummaryPanel) that only want the
		// summary/gap fields, never orbitals. includeIrreps=true additionally symmetry-labels each
		// band (irrep_tol/symprec passed straight to puntukas) - real per-band cost on top of the
		// orbital load itself, opt-in only (ElectronicStructurePanel's "Show symmetry labels").
		[[nodiscard]] Result<VaspOutputData> LoadOutput(
			const Path &directory,
			int bandStart = 0,
			int bandEnd = 10,
			bool includeOrbitals = true,
			bool includeIrreps = false,
			float irrepTol = 0.1f,
			float symprec = 1e-3f) const;

	private:
		ScriptRunner m_ScriptRunner;
	};

	// Parses a JSON payload matching vasp_output_load.py's output schema - the same parsing
	// LoadOutput itself uses on the subprocess's stdout, exposed separately so a cached copy of
	// that payload (CalculationSummaryPanel's on-disk cache) can be parsed back without re-running
	// the subprocess.
	[[nodiscard]] Result<VaspOutputData> ParseVaspOutputJson(const std::string &jsonPayload);
} // namespace DefectStudio
