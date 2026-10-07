#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	// task/86: the FNV (Freysoldt-Neugebauer-Van de Walle) charge correction, step 4 of the plan in
	// docs/work/project/plans/2026-10-06-fnv-review.md. Split in two on purpose:
	//
	// - step 1 (Python, pymatgen-analysis-defects, slow - two LOCPOT reads): the lattice energy
	//   E_lat and, per lattice axis, the planar-averaged DFT potential difference dV(x) and the model
	//   potential V_model(x). Small arrays, kept in FnvProfiles.
	// - step 2 (here, C++, instant): the alignment C = mean(dV - V_model) over a plateau window far
	//   from the defect, and E_corr = E_lat + q * C. Moving the window never re-runs Python.
	//
	// Units: Angstrom and eV throughout. `q` is the VASP charge state (q = -1 for one extra
	// electron); sxdefectalign takes the opposite sign (--charge = -q), which the bridge hides.

	// The constant (G = 0) term of the Gaussian model potential. pymatgen divides it by epsilon,
	// sxdefectalign does not; on GeN q-1 that is a uniform 3.66 meV shift of V_model, i.e. +3.4 meV
	// on E_corr for |q| = 1 and q^2 times that for larger charges. Profiles always arrive in the
	// pymatgen convention; the shift to the sxdefectalign one is
	//     V_sx(x) = V_pm(x) + (epsilon - 1) * mean(V_pm),
	// measured on GeN q-1 (sx mean -0.00444 eV = 5.70 x pymatgen mean -0.00078 eV, epsilon = 5.7).
	// Sxdefectalign is the default because the BSc-thesis numbers - the acceptance test - came from it.
	enum class FnvModelConvention
	{
		Sxdefectalign,
		Pymatgen,
	};

	struct FnvAxisProfile
	{
		double length = 0.0;            // |lattice vector|, A
		std::vector<double> position;   // grid points i * length / N, A (periodic endpoint excluded)
		std::vector<double> deltaV;     // planar average of V_defect - V_bulk, eV
		std::vector<double> modelPotential; // V_model, eV, pymatgen convention
	};

	struct FnvProfiles
	{
		int charge = 0;
		double dielectric = 1.0;
		glm::dvec3 centreFractional = glm::dvec3(0.0);
		double energyCutoff = 0.0;      // eV, the model's plane-wave cutoff
		double latticeEnergy = 0.0;     // E_lat, eV
		double modelPotentialMean = 0.0; // mean of V_model over the cell, pymatgen convention, eV
		std::array<FnvAxisProfile, 3> axes;
	};

	// V_model of one axis in the requested convention (see FnvModelConvention).
	[[nodiscard]] std::vector<double> FnvModelPotential(
		const FnvProfiles &profiles, int axis, FnvModelConvention convention);

	// A window on one axis, periodic: every point within `width / 2` of `centre` (A) counts.
	struct FnvPlateauWindow
	{
		double centre = 0.0;
		double width = 0.0;
	};

	// 4 bohr and 0.01 eV: the values the sxdefectalign workflow (scripts/python/fnv_sxdefectalign.py
	// propose_window) used for the whole diamond set.
	inline constexpr double kFnvMinimumPlateauWidth = 4.0 * 0.529177210903;
	inline constexpr double kFnvPlateauTolerance = 0.01;
	// Points closer than this to the defect are the model's core, left out of the slope fit.
	inline constexpr double kFnvSlopeCoreRadius = 5.0 * 0.529177210903;

	struct FnvAxisAlignment
	{
		FnvPlateauWindow window;
		double defectPosition = 0.0; // A, where |V_model| peaks
		bool flat = false;           // the proposal found a window with spread <= tolerance
		double alignment = 0.0;      // C = mean(dV - V_model) in the window, eV
		double spread = 0.0;         // population std of dV - V_model in the window, eV
		// Least-squares slope of dV against V_model outside kFnvSlopeCoreRadius: ~1 = localised
		// charge (FNV valid), < 1 partly delocalised, ~0 = FNV meaningless.
		double slope = 0.0;
	};

	// The automatic proposal, a port of propose_window: the defect sits where |V_model| peaks; the
	// window is centred on the point opposite it (defect + length / 2, wrapped into [0, length));
	// a grid point is inside when its periodic distance from that centre is <= width / 2; its width is the
	// widest one, stepping by the grid spacing from `minimumWidth` up to length / 2, whose residual
	// spread is <= `tolerance`. No such width: `flat` false and the minimum width is used.
	[[nodiscard]] FnvAxisAlignment ProposeFnvAlignment(
		const FnvAxisProfile &axis, const std::vector<double> &modelPotential,
		double minimumWidth = kFnvMinimumPlateauWidth, double tolerance = kFnvPlateauTolerance);

	// The same numbers for a window the user moved: alignment, spread and slope recomputed, `flat`
	// = spread <= kFnvPlateauTolerance. An empty window (no grid point inside) gives alignment 0 and
	// flat false.
	[[nodiscard]] FnvAxisAlignment AlignFnvInWindow(
		const FnvAxisProfile &axis, const std::vector<double> &modelPotential, const FnvPlateauWindow &window);

	struct FnvCorrection
	{
		double latticeEnergy = 0.0;
		double alignment = 0.0;      // mean C over the used axes
		double alignmentSpread = 0.0; // max C - min C over the used axes
		double correction = 0.0;     // E_corr = E_lat + q * C
	};

	// Combines the per-axis alignments of the axes with `useAxis` set (none set -> all three).
	[[nodiscard]] FnvCorrection CombineFnvCorrection(
		const FnvProfiles &profiles, const std::array<FnvAxisAlignment, 3> &alignments,
		const std::array<bool, 3> &useAxis = {true, true, true});
} // namespace DefectStudio
