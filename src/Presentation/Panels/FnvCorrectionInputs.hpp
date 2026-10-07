#pragma once

#include <optional>

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
} // namespace DefectStudio
