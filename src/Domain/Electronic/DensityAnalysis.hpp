#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Electronic/ElectronicStructureModel.hpp"

namespace DefectStudio
{
	// task/85: where a spin density (or a density difference) sits, and how much of it sits there.
	// This is step 3 of the FNV plan (docs/work/project/plans/2026-10-06-fnv-review.md): the centre
	// is the starting point for the FNV `--center`, and the localisation fraction is the check that
	// says whether an FNV correction means anything at all - Ge_C/Si_C +-1 in diamond put the charge
	// in the bulk band, and FNV happily returns a number for them anyway.
	//
	// Everything works on |value|: a magnetization block and a q-1 minus q0 difference both have
	// sign changes, and it is the magnitude that is localised, not the signed sum.
	//
	// Grid convention as OrbitalGridData: values[(i * NY + j) * NZ + k] is the point at fractional
	// (i/NX, j/NY, k/NZ), cell[a] is lattice vector a, `origin` is added to every cartesian result.

	struct DensityCentre
	{
		glm::vec3 fractional = glm::vec3(0.0f); // each component in [0, 1)
		glm::vec3 cartesian = glm::vec3(0.0f);  // cell * fractional + origin, Angstrom
	};

	// Centroid of |value| on the periodic cell. Per lattice direction: the |value|-weighted circular
	// mean of the fractional coordinate (so a blob straddling a cell face is not averaged into the
	// middle of the cell), then one refinement pass - the weighted mean of the minimum-image offsets
	// from that estimate - which removes the circular mean's pull towards the cell centre for a blob
	// of finite width. A direction along which the density is uniform (resultant of the circular
	// mean below 1e-6 of the total weight) has no centre: it gets 0.5 and no refinement. nullopt for
	// an empty grid or one that is zero everywhere.
	[[nodiscard]] std::optional<DensityCentre> AbsoluteDensityCentroid(const OrbitalGridData &grid);

	// Cumulative |value| as a function of the distance from `cartesianCentre`, using the
	// minimum-image distance on the periodic cell. Bin b holds every point with distance in
	// [b * binWidth, (b + 1) * binWidth); `cumulative[b]` is the sum of |value| * voxelVolume over
	// bins 0..b, so the last entry equals `total`.
	//
	// `ponytail:` minimum image by wrapping fractional offsets into [-0.5, 0.5) - exact for the
	// near-cubic supercells used for defects; a strongly skewed cell can need a neighbour-image
	// search, add it when such a cell shows up.
	struct DensityRadialProfile
	{
		float binWidth = 0.05f;
		std::vector<double> cumulative;
		double total = 0.0;

		// Fraction of `total` within `radius` (inclusive of the bin containing it, linearly
		// interpolated inside that bin). 0 for an empty profile; clamps to [0, 1].
		[[nodiscard]] float FractionWithin(float radius) const;
		// Smallest radius holding at least `fraction` of the total, from the same interpolation.
		// 0 for an empty profile.
		[[nodiscard]] float RadiusHolding(float fraction) const;
	};

	// Computes the profile in one pass over the grid. Voxel volume = |det(cell)| / point count, so
	// for a CHGCAR in e/A^3 `total` is the integral of |value| (the "|integral|" of DensityGrid).
	[[nodiscard]] DensityRadialProfile AbsoluteDensityRadialProfile(
		const OrbitalGridData &grid, const glm::vec3 &cartesianCentre, float binWidth = 0.05f);

	// Below this fraction within the analysis radius the N panel warns that the density is
	// delocalised and an FNV correction for this state is not meaningful. Measured on the diamond
	// set (|m| within 5 A of its centroid, 512-atom cell): GeV q-1 0.93, GeV q+1 0.90 (localised);
	// Ge_C q+1 0.21, Ge_C q-1 0.25 (delocalised - the FNV slope analysis flagged the same states).
	inline constexpr float kLocalisedFractionWarning = 0.5f;
} // namespace DefectStudio
