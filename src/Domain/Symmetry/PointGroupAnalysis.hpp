#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	// One coefficient of a symmetry-adapted basis vector, carried in both forms on purpose. The
	// exact form is what the analysis is actually about - a projection coefficient is sqrt(2)/2, not
	// 0.7071067811865476 - while the numeric form is what any renderer or numeric consumer needs.
	// SymPy objects do not JSON-serialize, so the exact form crosses the bridge as its string
	// representation and is never rebuilt into a symbolic type here: C++ treats it as an opaque
	// display/round-trip token, not as something to evaluate.
	struct ExactCoefficient
	{
		std::string exact;    // SymPy str(), e.g. "sqrt(2)/2", "-1/2", "0"
		double numeric = 0.0; // float() of that same expression
	};

	// One named site of the basis a representation is built on. For the NV- dangling-bond basis
	// these are the nitrogen and the three carbons.
	struct BasisSite
	{
		std::string label;                  // "d", "a", "b", "c"
		glm::dvec3 position{0.0, 0.0, 0.0}; // Cartesian, expressed in the point group's own frame
	};

	// A permutation representation: the point group permutes the basis sites among themselves, and
	// that permutation is the representation. Positions are sent rather than element matrices so the
	// two languages never have to agree on the order of the group elements.
	//
	// ponytail: permutation bases only. An orbital basis (p, d, ...) transforms by rotation matrices
	// rather than by permutation and needs its element matrices supplied directly - add an optional
	// explicit-matrix field here when the first such basis appears (workstream 5/6). Do not fake one
	// with positions.
	struct PermutationRepresentationRequest
	{
		std::string pointGroupLabel; // groupy PointGroup name, e.g. "C3v"
		std::vector<BasisSite> sites;
		// A site rotated by a group element must land within this distance of exactly one site, or
		// the request is rejected. The tutorial's unguarded argmin would instead silently accept a
		// basis the group does not actually close over.
		double matchTolerance = 1e-6;
	};

	struct IrrepMultiplicity
	{
		std::string irrepLabel; // "A1", "A2", "E"
		int multiplicity = 0;   // how many times it occurs in the reducible representation
		int dimension = 0;      // 1 for A1/A2, 2 for E
	};

	// One symmetry-adapted basis vector projected out of the reducible representation.
	//
	// Labels stay neutral (irrep + occurrence + row) on purpose. Physical names such as a1' vs a1 -
	// "lower" vs "upper" A1 in the NV- literature - encode an energy ordering that group theory
	// alone cannot determine; it takes the actual Hamiltonian. Naming them here would be a guess
	// dressed as a result. The UI assigns physical labels later, visibly marked as an assumption.
	struct SymmetryAdaptedVector
	{
		std::string irrepLabel;  // "A1", "E"
		int occurrenceIndex = 0; // 0-based: which copy of this irrep, in projection order
		int irrepRow = 0;        // 0-based row of a degenerate irrep (0 and 1 for E)
		// One coefficient per request site, in request order.
		std::vector<ExactCoefficient> coefficients;
	};

	struct PointGroupReduction
	{
		std::string pointGroupLabel;
		std::vector<std::string> siteLabels; // echoed back, in coefficient order
		int groupOrder = 0;
		// Only irreps with multiplicity > 0, in the point group's own irrep order.
		std::vector<IrrepMultiplicity> decomposition;
		std::vector<SymmetryAdaptedVector> projectedVectors;
	};
} // namespace DefectStudio
