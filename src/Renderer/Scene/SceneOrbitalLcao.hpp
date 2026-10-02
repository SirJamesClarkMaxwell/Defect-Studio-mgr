#pragma once

#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "Domain/Symmetry/PointGroupBasis.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// task/53: a symmetry-adapted one-electron orbital from the Group Theory panel, drawn in the
	// scene. The panel's projected vectors are coefficients over a site basis (for NV-: N and three
	// C around the vacancy); each site contributes one atomic function, and the orbital is
	//     psi = sum_i c_i * phi_i
	// built as a SceneOrbital with one LcaoComponent per site (RendererWindowState.hpp).

	// The function each basis site contributes.
	enum class SalcBasisFunction
	{
		// An sp3 lobe on the site, pointing at the analysis centre - the dangling bond. The default,
		// and the textbook basis for vacancy complexes (NV-, SiV, V_B).
		Sp3DanglingBond,
		// A p orbital on the site, pointing at the centre.
		PTowardCentre,
		// An s orbital on the site; nothing to aim.
		S,
	};

	// One LcaoComponent per basis site whose |coefficient| >= 1e-9, in site order:
	// - anchorAtom = basis.atomIndices[i], center = that atom's current position in
	//   windowState.structure (the fallback when the anchor goes stale);
	// - preset Sp3 lobe 0 / P lobe 0 (p_z) / S; shell = max(ValenceShell(element), 2) for Sp3 and P,
	//   ValenceShell(element) for S; effectiveCharge = ValenceEffectiveCharge(element);
	// - rotationEuler aims the member axis along -basis.sites[i].position - from the site toward the
	//   centre, taken from the basis's UNWRAPPED positions so a periodic defect straddling a cell
	//   face still points inward (AimSceneOrbitalEuler on an identity-oriented orbital); zero for S;
	// - coefficient = vector.coefficients[i].numeric.
	// The rest of the SceneOrbital is MakeDefaultSceneOrbital's defaults; displayName is the
	// caller's (the panel owns irrep/physical labels).
	//
	// Errors (StructuredError, category Validation):
	//   "orbital.salc.site_count_mismatch" - coefficients.size() != basis.sites.size() or
	//                                        != basis.atomIndices.size()
	//   "orbital.salc.complex_coefficient" - any |numericImaginary| >= 1e-9. Complex irreps (C3, C4h
	//                                        ...) need the real combination of the pair; ponytail
	//                                        until someone needs it.
	//   "orbital.salc.atom_out_of_range"   - a basis atom index >= windowState.structure.atoms.size()
	//   "orbital.salc.all_zero"            - no component survives
	[[nodiscard]] Result<RendererWindowState::SceneOrbital> BuildSalcSceneOrbital(
		const RendererWindowState &windowState,
		const SymmetryAdaptedVector &vector,
		const SelectionBasis &basis,
		SalcBasisFunction function,
		std::string displayName);
} // namespace DefectStudio
