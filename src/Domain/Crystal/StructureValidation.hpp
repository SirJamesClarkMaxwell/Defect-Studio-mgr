#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	// The single definition of "this structure is fit to be written and registered". A draft may be
	// transiently invalid while the user edits it, but it must not cross either of the two gates that
	// call this: the "Move to Structure Hub" hand-off, and AddStructureToProjectJob before it writes.
	//
	// Covers the persistence contract (finite non-degenerate cell, non-empty species, finite
	// coordinates) which is also exactly what keeps the renderer safe - a NaN cell vector is an
	// unbounded draw call, not a cosmetic problem.
	[[nodiscard]] Result<void> ValidateStructureForPersistence(const CrystalStructure &structure);
} // namespace DefectStudio
