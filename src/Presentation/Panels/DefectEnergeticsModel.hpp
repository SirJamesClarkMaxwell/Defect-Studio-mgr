#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Domain/Defects/CalculationCatalogue.hpp"
#include "Domain/Defects/DefectEnergetics.hpp"
#include "Domain/Defects/FnvCorrection.hpp"
#include "IO/EnergeticsIO.hpp"
#include "IO/FnvCorrectionsIO.hpp"
#include "IO/VaspRunRecordIO.hpp"

namespace DefectStudio
{
	// task/99: everything the "Energetyka defektów" panel shows and exports, built from the two
	// project files (energetics.yaml, fnv_corrections.yaml) and the bulk's band edges. Pure: no UI,
	// no file access, so the panel only draws it and the tests check it.
	// Plan: docs/work/project/plans/2026-10-11-energetics-auto-and-mu.md.

	struct EnergeticsRow
	{
		std::string defect;
		int charge = 0;
		SpeciesCounts addedAtoms;
		std::vector<std::size_t> candidates;   // runs of this (defect, q), indices into file.runs
		std::optional<std::size_t> energyRun;  // the user's choice (when it is a known run), else the proposal
		bool userChoice = false;
		std::string proposalReason;
		Path locpot;                           // the user's LOCPOT, else <energy run>/LOCPOT when it has one, else empty
		std::optional<std::size_t> fnvEntry;   // index into the FNV file's corrections whose defectLocpot == locpot
		// From the entry's profiles and windows (AlignFnvInWindow per axis in the entry's convention,
		// CombineFnvCorrection with its useAxis). Unset when there is no entry or it has no profiles.
		std::optional<FnvCorrection> correction;
		std::array<FnvAxisAlignment, 3> alignments{};
		bool included = true;
		// In words, Polish: "brak E_corr (FNV)" for a charged row without a correction, "FNV: q = +1,
		// wiersz q = -1" for an entry of another charge, "metoda: <mismatch>" per MethodMismatches with
		// the bulk, "obliczenie niezbieżne" when the energy run is not CalculationUsable.
		std::vector<std::string> problems;
	};

	struct EnergeticsMuSetView
	{
		std::string name;      // a vertex: its limiting phases joined with ", " ("Ge, Si, N"); else the user's name
		SpeciesEnergies deltaMu;
		bool vertex = false;
		std::vector<std::string> violated; // ViolatedChemicalPotentialBounds; always empty for a vertex
	};

	struct EnergeticsModel
	{
		std::vector<std::size_t> bulkCandidates;  // BulkCandidates
		std::optional<std::size_t> bulk;          // file.bulkRun when it is a known run, else the only candidate
		std::optional<VaspRunRecordIO::BandEdges> edges;
		FormationReference reference;             // bulk TOTEN, VBM, gap (file.gap overrides CBM - VBM)
		// species = the bulk's elements, then every other element of the rows' addedAtoms and of the
		// competing phases (sorted); references = file.references (TOTEN / atoms of that run), else
		// the bulk for a one-element bulk's element, else the only usable one-element run whose method
		// matches the bulk's; host = the bulk's composition reduced by the gcd ("C", "BN" ...).
		ChemicalPotentialSystem chemicalPotentials;
		std::string chemicalPotentialError;       // "brak referencji μ: Ge", or ChemicalPotentialVertices' error
		std::vector<EnergeticsMuSetView> muSets;  // vertices first, then file.muSets
		std::vector<EnergeticsRow> rows;          // visible groups, defect then charge descending
	};

	[[nodiscard]] EnergeticsModel BuildEnergeticsModel(const EnergeticsFile &file, const FnvCorrectionsFile &fnv,
		const std::optional<VaspRunRecordIO::BandEdges> &bulkEdges);

	// The row as a DefectChargeState (TOTEN of the energy run, E_corr; 0 for q = 0). nullopt when the
	// row has no energy run or its run has no TOTEN, or a charged row has no correction.
	[[nodiscard]] std::optional<DefectChargeState> EnergeticsRowState(
		const EnergeticsFile &file, const EnergeticsRow &row);

	// The files the TSV export writes, as (file name, UTF-8 text). Tab separated, one header line,
	// numbers with {:.10g}, empty cell = no value, "\n" line ends. Only included rows.
	// - "states.tsv": one line per (mu set, row), grouped by mu set (model.muSets order); header
	//   mu_set, defect, q, energy_run, locpot, E_tot (eV), E_bulk (eV), E_VBM (eV), E_g (eV),
	//   centre_f1, centre_f2, centre_f3, epsilon, convention, E_lat (eV), C (eV), C_spread (eV),
	//   E_corr (eV), then for a1, a2, a3: C_a1 (eV), sigma_a1 (eV), slope_a1, use_a1,
	//   window_centre_a1 (A), window_width_a1 (A); then E_f(E_F=0) (eV), E_f(E_F=E_g) (eV), problems.
	// - "transitions.tsv": defect, q_from, q_to, level (eV) - the envelope's levels from the VBM
	//   (mu-independent), one line each.
	// - "mu_sets.tsv": mu_set, vertex, species, mu_ref (eV), delta_mu (eV), mu (eV), violated.
	// - "formation_<k>_<name>.tsv" per mu set (k from 1, name with every UTF-8 code point outside
	//   [A-Za-z0-9_-] replaced by '_'): "E_F (eV)", one "<defect> q=<+1|0|-1> (eV)" column per row with
	//   a state, one "<defect> envelope (eV)" column per defect; E_F = 0, step, 2 step, ... up to and
	//   including the gap (the last point is the gap itself).
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> ExportEnergeticsTsv(
		const EnergeticsModel &model, const EnergeticsFile &file, const FnvCorrectionsFile &fnv, double step = 0.01);
} // namespace DefectStudio
