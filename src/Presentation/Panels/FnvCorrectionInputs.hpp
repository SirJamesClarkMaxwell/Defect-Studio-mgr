#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	// task/87: the guesses the FNV panel makes when a defect LOCPOT is picked, so the usual case is
	// "pick the file, press Compute". Each one only pre-fills a field the user can still edit.

	// The charge state from the calculation folder's name: "q-1", "q+1", "q0", "q-2" (the Okeanos
	// layout, data/<defect>/q-1/LOCPOT) or the thesis "Stan_1-", "Stan_2-", "stan_+1" (sign before or
	// after the digits, case-insensitive). nullopt when the folder name says nothing.
	// `ponytail:` folder names only - q = sum(N * ZVAL) - NELECT from the OUTCAR when a folder is
	// named otherwise.
	[[nodiscard]] std::optional<int> ChargeFromCalculationFolder(const Path &locpot);

	// The bulk reference next to a defect calculation: `<ancestor>/bulk/LOCPOT` for the 2nd and 3rd
	// ancestors of the LOCPOT (data/GeV/q-1/LOCPOT -> data/bulk/LOCPOT), first one that exists.
	[[nodiscard]] std::optional<Path> FindBulkLocpotNear(const Path &defectLocpot);

	// `cartesian` in fractional coordinates of `lattice` (columns are the lattice vectors), each
	// component wrapped into [0, 1). nullopt for a singular lattice.
	[[nodiscard]] std::optional<glm::dvec3> FractionalCoordinates(const glm::mat3 &lattice, const glm::vec3 &cartesian);

	// The structure a LOCPOT was computed for: the CONTCAR next to it (full precision), else the
	// LOCPOT's own header (6 decimals). VASP 5 with Direct coordinates only; nullopt otherwise.
	struct VaspStructureHeader
	{
		std::array<glm::dvec3, 3> lattice{}; // lattice vectors, A (scale applied)
		std::vector<std::string> species;    // one per atom
		std::vector<glm::dvec3> fractional;  // one per atom
	};
	[[nodiscard]] std::optional<VaspStructureHeader> ReadLocpotStructure(const Path &locpot);

	// The defect centre the thesis runs used: centroid of the atoms whose species the bulk does not
	// have (GeV -> the Ge, GeNV -> midway between Ge and N), minimum image around the first one,
	// wrapped into [0, 1). `species` names them ("Ge+N"). nullopt when the defect adds no new species.
	// ponytail: impurities only - a plain vacancy needs the missing bulk site; add when one is computed.
	struct ImpurityCentre
	{
		glm::dvec3 fractional{};
		std::string species;
	};
	[[nodiscard]] std::optional<ImpurityCentre> FindImpurityCentre(
		const VaspStructureHeader &defect, const VaspStructureHeader &bulk);

	// "a1 (x)" when lattice vector i lies along a Cartesian axis (within ~1 degree), else "a1" - the
	// planar averages run along lattice vectors, like sxdefectalign's vline-eV-a0/a1/a2.
	[[nodiscard]] std::array<std::string, 3> FnvAxisLabels(const std::array<glm::dvec3, 3> &lattice);
} // namespace DefectStudio
