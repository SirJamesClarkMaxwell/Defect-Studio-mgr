#pragma once

#include <cstddef>
#include <utility>

#include "Domain/Vibrations/CcdAnalysis.hpp"

namespace DefectStudio
{
	// Absent/empty for invalid geometry or zero total displacement weight.
	[[nodiscard]] std::optional<glm::dvec3> DisplacementCentre(const CcdDisplacement &displacement);
	[[nodiscard]] std::vector<std::pair<double, double>> LocalisationCurve(const CcdDisplacement &displacement);
	// Zero for invalid weights or fraction outside (0, 1].
	[[nodiscard]] std::size_t AtomsCarrying(const CcdDisplacement &displacement, double fraction);
	// Ordered atoms only: no assignment, rotation or alignment. Mass tolerance is 1e-3 amu.
	[[nodiscard]] std::optional<double> DisplacementOverlap(const CcdDisplacement &a, const CcdDisplacement &b);
}
