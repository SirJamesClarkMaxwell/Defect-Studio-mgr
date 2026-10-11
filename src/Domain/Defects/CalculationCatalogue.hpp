#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Domain/Defects/DefectEnergetics.hpp"

namespace DefectStudio
{
	// task/97: what the energetics module knows about one VASP run without opening anything big -
	// filled by IO (ReadVaspRunRecord: the head and the tail of OUTCAR), grouped and ranked here.
	// Plan: docs/work/project/plans/2026-10-11-energetics-auto-and-mu.md.

	// The settings two runs must share for their energies to be subtracted.
	struct CalculationMethod
	{
		bool hybrid = false;   // LHFCALC
		double hfscreen = 0.0; // HFSCREEN
		double aexx = 0.0;     // AEXX
		std::string precFock;  // PRECFOCK, lower case ("normal", "fast"); empty without HF
		double encut = 0.0;    // ENCUT, eV
		std::string prec;      // PREC, lower case
		int ispin = 1;
		std::vector<std::string> potcars; // TITEL per species, e.g. "PAW_PBE Ge_d 03Jul2007"
	};

	struct VaspRunRecord
	{
		Path directory; // the run folder (contains OUTCAR)
		std::vector<std::string> species; // per POTCAR, element symbols ("Ge" from "Ge_d")
		std::vector<int> ions;            // ions per type, same order
		std::vector<double> valence;      // ZVAL per type, same order
		std::optional<double> electrons;  // NELECT
		std::optional<double> totalEnergy; // last "free  energy   TOTEN"
		bool finished = false;            // the timing footer is there
		bool relaxation = false;          // NSW > 0 and IBRION in {1, 2, 3}
		bool ionicConverged = false;      // "reached required accuracy"
		CalculationMethod method;
		bool hasLocpot = false;
		bool hasContcar = false;
		// Cache key: a summary is re-read only when OUTCAR's size or write time changed.
		std::uintmax_t outcarSize = 0;
		std::int64_t outcarWriteTime = 0;
		std::string error; // non-empty: OUTCAR could not be read or parsed
	};

	// q = sum_s N_s ZVAL_s - NELECT, when that is an integer within 1e-3; nullopt otherwise.
	[[nodiscard]] std::optional<int> CalculationCharge(const VaspRunRecord &record);
	// Atoms per element (species repeated in POTCAR are summed).
	[[nodiscard]] SpeciesCounts CalculationComposition(const VaspRunRecord &record);
	// Usable as an energy: read without error, finished, TOTEN present, and converged if a relaxation.
	[[nodiscard]] bool CalculationUsable(const VaspRunRecord &record);
	// Every mismatch that forbids subtracting the two energies, in words ("ENCUT 520 vs 600"): hybrid,
	// HFSCREEN, AEXX, PRECFOCK, ENCUT, PREC, and the POTCAR of every species both runs have. ISPIN is
	// not compared - a closed-shell bulk next to a spin-polarised defect is the usual pairing.
	// Empty = compatible.
	[[nodiscard]] std::vector<std::string> MethodMismatches(const CalculationMethod &a, const CalculationMethod &b);

	// The charge a folder name states: "q-1", "q+1", "q0" (data/<defect>/q-1) or the thesis "Stan_1-",
	// "Stan_2-", "stan_+1" (sign before or after the digits, case-insensitive). nullopt otherwise.
	[[nodiscard]] std::optional<int> ChargeFromFolderName(std::string_view name);

	// The defect runs of a scan, by (defect, q). `defect` is the first folder below `root` on the
	// run's path ("O:/GeV/Stan_1-/HSE/PrecFock_Normal" under "O:/" -> "GeV"); when that folder is a
	// charge folder (ChargeFromFolderName), the root itself is the defect folder and its name is used
	// (root "O:/GeV" -> "GeV"). q is CalculationCharge. Not defect runs: runs outside `root`, the
	// bulk, runs whose composition equals the bulk's, differs from it by more than `maxChangedAtoms`
	// atoms in total (references, other cells; GeN2V changes 7), or whose q is unknown. Groups sorted by defect, then
	// q descending; runs inside by path.
	struct DefectRunGroup
	{
		std::string defect;
		int charge = 0;
		SpeciesCounts addedAtoms;         // composition minus the bulk's
		std::vector<std::size_t> runs;    // indices into the records
	};
	[[nodiscard]] std::vector<DefectRunGroup> GroupDefectRuns(std::span<const VaspRunRecord> records,
		std::size_t bulk, const Path &root, int maxChangedAtoms = 10);

	// Bulk candidates: usable, q = 0, the largest ion count of any usable run (the supercell, not a
	// primitive cell or a reference), and no element outside those every such largest run has (so
	// Ge_C with 511 C + 1 Ge is not a bulk of diamond, and hBN with B and N still is). Sorted by path.
	[[nodiscard]] std::vector<std::size_t> BulkCandidates(std::span<const VaspRunRecord> records);

	// The proposed run for one group's energy (TOTEN) and the reason, or nullopt with the reason when
	// the choice must be the user's. Rules, first that separates wins:
	// 1. usable (CalculationUsable);
	// 2. method compatible with the bulk (MethodMismatches empty);
	// 3. PRECFOCK "normal" or "accurate" over "fast";
	// 4. with LOCPOT (the FNV run is then the same run).
	// Two or more runs left = nullopt ("wybierz"). Never by TOTEN or date.
	struct RunProposal
	{
		std::optional<std::size_t> run; // index into the records
		std::string reason;
	};
	[[nodiscard]] RunProposal ProposeEnergyRun(
		std::span<const VaspRunRecord> records, const DefectRunGroup &group, std::size_t bulk);
} // namespace DefectStudio
