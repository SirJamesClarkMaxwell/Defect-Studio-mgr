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
		double numeric = 0.0; // float() of that same expression (real part for complex characters)
		double numericImaginary = 0.0; // non-zero only for complex characters (e.g. C3, C4h irreps)
	};

	// One named site of the basis a representation is built on. For the NV- dangling-bond basis
	// these are the nitrogen and the three carbons.
	struct BasisSite
	{
		std::string label;                  // "d", "a", "b", "c"
		glm::dvec3 position{0.0, 0.0, 0.0}; // Cartesian. Spike request: point group's own frame.
		                                    // Analysis request: any frame, centred on the analysis centre.
		// Empty = match any site. When set, a group element may only map a site onto a site of the
		// same element (N never permutes with C).
		std::string element;
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

	// --- Task 23: full analysis for the group-theory panel -------------------------------------

	struct CharacterTable
	{
		std::string pointGroupLabel;
		int groupOrder = 0;
		std::vector<std::string> classLabels; // groupy class names, e.g. {"E", "2C3", "3sv"}
		std::vector<int> classSizes;          // parallel to classLabels, e.g. {1, 2, 3}
		std::vector<std::string> irrepLabels; // groupy irrep order, e.g. {"A1", "A2", "E"}
		std::vector<int> irrepDimensions;     // parallel to irrepLabels
		// characters[irrep][class], same orders as irrepLabels / classLabels.
		std::vector<std::vector<ExactCoefficient>> characters;
	};

	struct PointGroupDetection
	{
		bool ran = false;        // false when the request named a group manually
		bool determined = false; // false = Undetermined; `reason` says why, nothing else is filled
		std::string pointGroupLabel; // groupy label the rest of the result uses, e.g. "C3v"
		std::string detectorSymbol;  // raw pymatgen PointGroupAnalyzer sch_symbol, e.g. "C3v"
		double tolerance = 0.0;      // Å, the tolerance the detector ran with
		std::string reason;
	};

	// One many-electron term from groupy ActiveSpace.term_table().
	struct MultipletTerm
	{
		std::string irrepLabel;   // "A2", "E", "A1"
		int spinMultiplicity = 0; // 2S+1
		int irrepDimension = 0;   // dΓ
		int countPerRow = 0;      // how many times this (Γ, S) term occurs
		int totalStates = 0;      // countPerRow * dΓ * (2S+1)
	};

	struct PointGroupAnalysisRequest
	{
		// Empty = detect with pymatgen PointGroupAnalyzer on the sites; otherwise a groupy label.
		std::string pointGroupLabel;
		// Centred on the analysis centre, in the structure's Cartesian frame. The bridge rotates them
		// into groupy's standard frame itself (see PointGroupAnalysisResult::frameRotation).
		std::vector<BasisSite> sites;
		// Å. Used by detection AND by the permutation closure check - relaxed defect geometries are
		// never closed at 1e-6.
		double symmetryTolerance = 0.1;
		// Multiplets: orbital irreps the user marks active + electron count. Empty irreps or
		// activeElectronCount == 0 = no multiplet computation.
		std::vector<std::string> activeOrbitalIrreps;
		int activeElectronCount = 0;
	};

	struct PointGroupAnalysisResult
	{
		PointGroupDetection detection;
		// Maps request Cartesian coordinates into groupy's standard frame: p_groupy = frameRotation * p.
		// Identity when the sites already are in that frame.
		glm::dmat3 frameRotation{1.0};
		CharacterTable characterTable;
		std::vector<ExactCoefficient> reducibleCharacters; // one per class, characterTable order
		PointGroupReduction reduction;
		std::vector<MultipletTerm> multiplets; // term_table order
		int multipletTotalStates = 0;
	};
} // namespace DefectStudio
