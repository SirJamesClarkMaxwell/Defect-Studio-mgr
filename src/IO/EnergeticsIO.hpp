#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Domain/Defects/CalculationCatalogue.hpp"

namespace DefectStudio
{
	// task/98: the energetics module's project file, energetics.yaml next to manifest.yaml and
	// fnv_corrections.yaml. It holds what the user decided (which run is the converged one, which
	// LOCPOT goes to FNV, the bulk, the mu references and sets) and the scan itself, so reopening a
	// project does not walk the Okeanos mount again (249 s for O:/). FNV windows and profiles stay in
	// fnv_corrections.yaml, keyed by the LOCPOT path chosen here.
	// Plan: docs/work/project/plans/2026-10-11-energetics-auto-and-mu.md.

	// The user's decisions for one (defect, q). Unset = follow the proposal (ProposeEnergyRun), so a
	// rescan that finds a better run updates it; set = the user's choice, never replaced.
	struct EnergeticsChoice
	{
		std::string defect;
		int charge = 0;
		std::optional<Path> energyRun; // folder whose TOTEN is used
		std::optional<Path> locpot;    // LOCPOT file for FNV; unset = <energy run>/LOCPOT
		bool included = true;          // in the plots, the envelope and the export
	};

	// A competing phase: the run and how many formula units its cell holds (formula x units = the
	// run's composition, checked when used).
	struct EnergeticsPhase
	{
		std::string name;
		SpeciesCounts formula;
		Path run;
	};

	struct EnergeticsMuSet
	{
		std::string name;
		SpeciesEnergies deltaMu; // eV/atom, species absent = 0
	};

	struct EnergeticsFile
	{
		int formatVersion = 1;
		Path root;                    // the scanned folder
		std::vector<VaspRunRecord> runs; // the last scan, the cache
		std::optional<Path> bulkRun;  // unset = the only BulkCandidates entry
		std::vector<EnergeticsChoice> choices;
		std::map<std::string, Path> references; // element -> elemental run, mu_ref = TOTEN / atoms
		std::vector<EnergeticsPhase> competing;
		std::vector<EnergeticsMuSet> muSets;     // typed by the user; the vertices are recomputed
		double dielectric = 5.7;
		std::string dielectricSource;
		std::optional<double> gap;    // unset = CBM - VBM of the bulk run's EIGENVAL
		std::string gapSource;
	};

	// energetics.yaml. Paths inside the project are written relative to it, others absolute, '/'
	// separators (as FnvCorrectionsIO). Parse drops a bad entry with a warning and keeps the rest;
	// it fails only on non-YAML, a non-map root or a newer formatVersion. Missing file on Load =
	// success with an empty file. Save writes a sibling temp file and renames it over.
	class EnergeticsIO
	{
	public:
		static constexpr int kFormatVersion = 1;

		[[nodiscard]] static Path FilePath(const Path &projectDirectory);
		[[nodiscard]] static bool Parse(const std::string &text, const Path &projectDirectory,
			EnergeticsFile &outFile, std::vector<std::string> &outWarnings, std::string &outError);
		[[nodiscard]] static std::string Serialize(const EnergeticsFile &file, const Path &projectDirectory);
		[[nodiscard]] static bool Load(const Path &projectDirectory, EnergeticsFile &outFile,
			std::vector<std::string> &outWarnings, std::string &outError);
		[[nodiscard]] static bool Save(const Path &projectDirectory, const EnergeticsFile &file, std::string &outError);
	};
} // namespace DefectStudio
