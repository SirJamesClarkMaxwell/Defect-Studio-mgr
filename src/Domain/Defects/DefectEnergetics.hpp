#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DefectStudio
{
	// task/96: formation energies, transition levels and chemical-potential limits of point defects -
	// pure arithmetic on numbers already read from the calculations (no files, no UI). Energies in
	// eV, the Fermi level measured from the VBM. Conventions: .claude/skills/defect-formation-energy.
	//
	//   E_f(X^q; E_F) = E_tot(X^q) - E_tot(bulk) - sum_i n_i mu_i + q (E_VBM + E_F) + E_corr(X^q)
	//   mu_i = mu_i^ref + Delta mu_i

	// Atom counts per species ("C" -> -2, "Ge" -> +1), or a formula ("Si" -> 3, "N" -> 4).
	using SpeciesCounts = std::map<std::string, int>;
	using SpeciesEnergies = std::map<std::string, double>;

	// One charge state of one defect configuration.
	struct DefectChargeState
	{
		std::string defect; // "GeV"; states of one defect share it
		int charge = 0;
		double totalEnergy = 0.0; // last TOTEN of the defect run
		SpeciesCounts addedAtoms; // n_i = defect minus bulk, per species
		double correction = 0.0;  // E_corr (FNV), 0 for q = 0
	};

	struct FormationReference
	{
		double bulkEnergy = 0.0; // TOTEN of the bulk supercell
		double vbm = 0.0;        // E_VBM of the bulk run
		double gap = 0.0;        // E_F runs over [0, gap]
	};

	// E_f at `fermiLevel`; nullopt when `mu` has no value for a species in addedAtoms with n_i != 0.
	[[nodiscard]] std::optional<double> FormationEnergy(const DefectChargeState &state,
		const FormationReference &reference, const SpeciesEnergies &mu, double fermiLevel);

	// eps(q1/q2) = (E_f(q1; 0) - E_f(q2; 0)) / (q2 - q1): the Fermi level where the two lines cross.
	// `intercept` = E_f at E_F = 0 (mu cancels for one composition). nullopt when q1 == q2.
	[[nodiscard]] std::optional<double> TransitionLevel(double intercept1, int charge1, double intercept2, int charge2);

	// The lowest of the lines E_f = intercept[k] + charge[k] * E_F over [0, gap], left to right. Each
	// segment names the line (index into the inputs) that is lowest there; a line that is never the
	// lowest is absent, as are zero-length touches. Equal lines: the lower index wins. Exact
	// intersections, not a sampled curve. Empty for gap <= 0 or no lines.
	struct EnvelopeSegment
	{
		double from = 0.0;
		double to = 0.0;
		std::size_t line = 0;
	};
	[[nodiscard]] std::vector<EnvelopeSegment> LowerEnvelope(
		std::span<const double> intercepts, std::span<const int> charges, double gap);

	// The thermodynamic transition levels of one defect: where neighbouring envelope segments meet,
	// so a skipped charge (negative U) gives one (q/q') with |q - q'| > 1.
	struct ChargeTransition
	{
		int chargeFrom = 0; // the stable charge just below `level` (more positive)
		int chargeTo = 0;   // the stable charge just above it
		double level = 0.0; // from the VBM
	};
	[[nodiscard]] std::vector<ChargeTransition> EnvelopeTransitions(
		std::span<const EnvelopeSegment> envelope, std::span<const int> charges);

	// ---- Chemical potentials ----------------------------------------------------------------

	// A calculated phase: a formula unit and its energy (TOTEN of the cell / formula units in it).
	struct ChemicalPhase
	{
		std::string name;         // "SiC", "Si3N4", "hBN"
		SpeciesCounts formula;    // one formula unit
		double energyPerFormulaUnit = 0.0;
	};

	// Delta H_f per formula unit = E_fu - sum_i c_i mu_i^ref; nullopt when a species has no reference.
	[[nodiscard]] std::optional<double> FormationEnthalpy(const ChemicalPhase &phase, const SpeciesEnergies &references);

	// The thermodynamic limits on Delta mu:
	// - host:        sum_j c_j Delta mu_j = Delta H_f(host)   (diamond: Delta mu_C = 0)
	// - elemental:   Delta mu_i <= 0 for every species in `species`
	// - competing:   sum_k c_k Delta mu_k <= Delta H_f(phase) for every phase in `competing`
	// `species` is every element with a chemical potential (host and impurities), each needs a reference.
	struct ChemicalPotentialSystem
	{
		SpeciesEnergies references; // mu_i^ref, eV/atom (elemental phase; N2 -> E/2)
		std::vector<std::string> species;
		ChemicalPhase host;
		std::vector<ChemicalPhase> competing;
	};

	// One corner of the allowed region. Impurities make it unbounded towards negative Delta mu
	// (impurity-poor has no finite limit), so these are the corners only, every one of them a
	// "rich" condition of some kind. `limitingPhases` names the bounds that are tight there, in the
	// order elements (as in `species`) then competing phases (as in `competing`): an element ("Ge" =
	// Delta mu_Ge = 0) or a phase name. The host equality is always tight and not listed; the
	// elemental bound of a one-element host (Delta mu_C <= 0 in diamond) is the host equality itself
	// and is not a bound of its own.
	struct ChemicalPotentialVertex
	{
		SpeciesEnergies deltaMu; // every entry of `species`
		std::vector<std::string> limitingPhases;
	};

	struct ChemicalPotentialLimits
	{
		std::vector<ChemicalPotentialVertex> vertices; // deduplicated (1e-9 eV), sorted by limitingPhases
		std::string error;                             // non-empty: missing reference, empty region, ...
	};
	// Vertex enumeration: every choice of (free dimensions) tight bounds that has a unique solution
	// and violates no other bound by more than 1e-9 eV. Free dimensions = species - 1 (the host
	// equality); a host with more than one species is still one equality. Limited to 6 species.
	[[nodiscard]] ChemicalPotentialLimits ChemicalPotentialVertices(const ChemicalPotentialSystem &system);

	// The bounds a hand-typed set breaks by more than `tolerance` eV: the host equality (the host's
	// name), an element name or a competing phase name, in that order (elements as in `species`,
	// phases as in `competing`). Empty = the set is thermodynamically allowed. Missing Delta mu = 0.
	[[nodiscard]] std::vector<std::string> ViolatedChemicalPotentialBounds(
		const ChemicalPotentialSystem &system, const SpeciesEnergies &deltaMu, double tolerance = 1e-6);

	// mu_i = mu_i^ref + Delta mu_i for every species in the system (missing Delta mu = 0).
	[[nodiscard]] SpeciesEnergies AbsoluteChemicalPotentials(
		const ChemicalPotentialSystem &system, const SpeciesEnergies &deltaMu);
} // namespace DefectStudio
