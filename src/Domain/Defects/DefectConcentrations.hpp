#pragma once

#include "Domain/Defects/DefectEnergetics.hpp"

#include <variant>

namespace DefectStudio
{
	inline constexpr double kBoltzmannEvPerK = 8.617333262145e-5;

	struct ConcentrationChargeState
	{
		int charge = 0;
		double interceptEv = 0.0; // A_q = FormationEnergy at E_F = 0, at the chosen absolute mu
		double degeneracy = 1.0; // orientation times spin/electronic degeneracy
	};

	struct ConcentrationDefect
	{
		std::string defect;
		double siteDensityCm3 = 0.0; // available sites per cm^3, not the defect concentration
		std::vector<ConcentrationChargeState> chargeStates;
	};

	struct ConcentrationDefectInput
	{
		std::vector<DefectChargeState> chargeStates; // all states must name the same defect
		double siteDensityCm3 = 0.0;
		std::vector<double> degeneracies; // empty means 1 for every state; otherwise one per state
	};

	struct ConcentrationDefects
	{
		std::vector<ConcentrationDefect> defects;
		std::string error;
	};

	// Computes A_q in eV using FormationEnergy(state, reference, absoluteMu, 0); preserves input
	// order and degeneracies. On invalid inputs or missing mu, returns an error and no defects.
	[[nodiscard]] ConcentrationDefects BuildConcentrationDefects(
		std::span<const ConcentrationDefectInput> inputs, const FormationReference &reference,
		const SpeciesEnergies &absoluteMu);

	struct EffectiveMassBands
	{
		double electronDosMass = 0.0; // aggregate DOS mass / free electron mass (includes all valleys)
		double holeDosMass = 0.0; // aggregate DOS mass / free electron mass (includes all hole bands)
	};

	struct DosPoint
	{
		double energyEv = 0.0; // relative to VBM
		double statesPerEvCm3 = 0.0; // includes spin; no extra factor of two is applied
	};

	struct TabulatedDosBands
	{
		std::vector<DosPoint> valence; // strictly increasing energies <= 0, at least two samples
		std::vector<DosPoint> conduction; // strictly increasing energies >= gapEv, at least two samples
	};

	using ConcentrationCarrierBands = std::variant<EffectiveMassBands, TabulatedDosBands>;

	struct ChargeStateConcentration
	{
		std::size_t defectIndex = 0;
		std::size_t chargeStateIndex = 0;
		int charge = 0;
		double concentrationCm3 = 0.0;
	};

	struct DefectConcentrationSolution
	{
		double temperatureK = 0.0;
		double fermiLevelEv = 0.0; // from VBM; may lie outside the gap
		double electronsCm3 = 0.0;
		double holesCm3 = 0.0;
		double fixedChargeCm3 = 0.0; // signed, ionized donors positive
		std::vector<ChargeStateConcentration> chargeStates;
		std::vector<double> defectTotalsCm3; // same order as input defects
		double relativeNeutralityResidual = 0.0; // signed sum / largest |q*c|, p, n, |N_fixed|
		bool converged = false;
		std::string error;
	};

	// Solves sum(q*c) + p - n + fixedChargeCm3 = 0 by expanding [-1, gapEv+1] eV and bisection.
	// c = N_sites*g*exp(-(A_q+q*E_F)/(k_B*T)), in cm^-3. Carriers use Fermi-Dirac statistics:
	// parabolic N_c,v = 2*(2*pi*m*k_B*T/h^2)^(3/2), or integrals over piecewise-linear tabulated
	// DOS (zero outside its supplied range). T must be 1..3000 K and gapEv positive. Neutrality
	// is evaluated in log space; convergence requires |relative residual| <= 1e-10. Output density
	// underflow becomes zero; unrepresentable densities/totals are errors, never silent clipping.
	[[nodiscard]] DefectConcentrationSolution SolveDefectConcentrations(
		std::span<const ConcentrationDefect> defects, const ConcentrationCarrierBands &bands,
		double gapEv, double temperatureK, double fixedChargeCm3 = 0.0);

	struct DefectConcentrationGridPoint
	{
		std::size_t temperatureIndex = 0;
		std::size_t chemicalPotentialIndex = 0;
		std::size_t fixedChargeIndex = 0;
		DefectConcentrationSolution solution;
	};

	// Computes one independent solution per Cartesian product point, in T, absoluteMu, N_fixed
	// order (N_fixed varies fastest). Uses reference.gap in eV and rebuilds intercepts at each mu.
	// Failed points retain indices, T and N_fixed with an error; an empty axis gives no points.
	[[nodiscard]] std::vector<DefectConcentrationGridPoint> SolveDefectConcentrationGrid(
		std::span<const ConcentrationDefectInput> defects, const FormationReference &reference,
		const ConcentrationCarrierBands &bands, std::span<const double> temperaturesK,
		std::span<const SpeciesEnergies> absoluteMuSets, std::span<const double> fixedChargesCm3);
} // namespace DefectStudio
