#include "Core/dspch.hpp"

#include "Domain/Electronic/DensityAnalysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace DefectStudio
{
	namespace
	{
		bool ValidGrid(const OrbitalGridData &grid)
		{
			std::size_t count = 1;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (grid.dimensions[axis] <= 0 ||
					count > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(grid.dimensions[axis]))
					return false;
				count *= static_cast<std::size_t>(grid.dimensions[axis]);
				if (!std::isfinite(grid.origin[axis]))
					return false;
				for (int component = 0; component < 3; ++component)
					if (!std::isfinite(grid.cell[axis][component]))
						return false;
			}
			return count == grid.values.size();
		}
	} // namespace

	std::optional<DensityCentre> AbsoluteDensityCentroid(const OrbitalGridData &grid)
	{
		if (!ValidGrid(grid))
			return std::nullopt;
		// Axis marginals let both mean passes reuse one scan of the full grid.
		std::array<std::vector<double>, 3> weights;
		for (int axis = 0; axis < 3; ++axis)
			weights[axis].assign(grid.dimensions[axis], 0.0);
		double total = 0.0;
		std::size_t index = 0;
		for (int i = 0; i < grid.dimensions.x; ++i)
			for (int j = 0; j < grid.dimensions.y; ++j)
				for (int k = 0; k < grid.dimensions.z; ++k)
				{
					const double weight = std::abs(static_cast<double>(grid.values[index++]));
					if (!std::isfinite(weight))
						return std::nullopt;
					total += weight;
					weights[0][i] += weight;
					weights[1][j] += weight;
					weights[2][k] += weight;
				}
		if (!(total > 0.0))
			return std::nullopt;

		DensityCentre result;
		for (int axis = 0; axis < 3; ++axis)
		{
			const double dimension = grid.dimensions[axis];
			double cosine = 0.0, sine = 0.0;
			for (int i = 0; i < grid.dimensions[axis]; ++i)
			{
				const double angle = 2.0 * std::numbers::pi * i / dimension;
				cosine += weights[axis][i] * std::cos(angle);
				sine += weights[axis][i] * std::sin(angle);
			}
			if (std::hypot(cosine, sine) < 1e-6 * total)
			{
				result.fractional[axis] = 0.5f;
				continue;
			}
			double centre = std::atan2(sine, cosine) / (2.0 * std::numbers::pi);
			centre -= std::floor(centre);
			double offsetSum = 0.0;
			for (int i = 0; i < grid.dimensions[axis]; ++i)
			{
				double offset = i / dimension - centre;
				offset -= std::floor(offset + 0.5);
				offsetSum += weights[axis][i] * offset;
			}
			centre += offsetSum / total;
			centre -= std::floor(centre);
			const float fractional = static_cast<float>(centre);
			result.fractional[axis] = fractional - std::floor(fractional);
		}
		result.cartesian = grid.origin + grid.cell * result.fractional;
		return result;
	}

	float DensityRadialProfile::FractionWithin(const float radius) const
	{
		if (cumulative.empty() || !(total > 0.0) || !(binWidth > 0.0f) || !(radius > 0.0f))
			return 0.0f;
		const double position = static_cast<double>(radius) / binWidth;
		if (position >= static_cast<double>(cumulative.size()))
			return 1.0f;
		const std::size_t bin = static_cast<std::size_t>(position);
		const double previous = bin == 0 ? 0.0 : cumulative[bin - 1];
		const double mass = previous + (cumulative[bin] - previous) * (position - bin);
		return static_cast<float>(std::clamp(mass / total, 0.0, 1.0));
	}

	float DensityRadialProfile::RadiusHolding(const float fraction) const
	{
		if (cumulative.empty() || !(total > 0.0) || !(binWidth > 0.0f) || !(fraction > 0.0f))
			return 0.0f;
		const double target = std::min(static_cast<double>(fraction), 1.0) * total;
		const auto found = std::lower_bound(cumulative.begin(), cumulative.end(), target);
		if (found == cumulative.end())
			return static_cast<float>(cumulative.size()) * binWidth;
		const std::size_t bin = static_cast<std::size_t>(found - cumulative.begin());
		const double previous = bin == 0 ? 0.0 : cumulative[bin - 1];
		const double part = *found > previous ? (target - previous) / (*found - previous) : 0.0;
		return static_cast<float>((bin + part) * binWidth);
	}

	DensityRadialProfile AbsoluteDensityRadialProfile(
		const OrbitalGridData &grid, const glm::vec3 &cartesianCentre, const float binWidth)
	{
		DensityRadialProfile profile;
		if (!ValidGrid(grid) || !std::isfinite(binWidth) || !(binWidth > 0.0f))
			return profile;
		const glm::dmat3 cell(grid.cell);
		const double volume = std::abs(glm::determinant(cell));
		if (!std::isfinite(volume) || !(volume > 0.0))
			return profile;
		for (int axis = 0; axis < 3; ++axis)
			if (!std::isfinite(cartesianCentre[axis]))
				return profile;
		profile.binWidth = binWidth;
		const glm::dvec3 centre = glm::inverse(cell) * (glm::dvec3(cartesianCentre) - glm::dvec3(grid.origin));
		const double voxelVolume = volume / static_cast<double>(grid.values.size());
		const double maxRadius = 0.5 * (glm::length(cell[0]) + glm::length(cell[1]) + glm::length(cell[2]));
		profile.cumulative.assign(static_cast<std::size_t>(std::ceil(maxRadius / binWidth)) + 1, 0.0);
		std::size_t index = 0;
		for (int i = 0; i < grid.dimensions.x; ++i)
			for (int j = 0; j < grid.dimensions.y; ++j)
				for (int k = 0; k < grid.dimensions.z; ++k)
				{
					const double weight = std::abs(static_cast<double>(grid.values[index++]));
					if (!std::isfinite(weight))
						return {};
					if (weight == 0.0)
						continue;
					glm::dvec3 offset = glm::dvec3(i, j, k) / glm::dvec3(grid.dimensions) - centre;
					// ponytail: fractional wrapping is exact near cubic; search neighbour images for strongly skewed cells.
					offset -= glm::floor(offset + 0.5);
					const double radius = glm::length(cell * offset);
					const std::size_t bin = static_cast<std::size_t>(radius / binWidth);
					profile.cumulative[bin] += weight * voxelVolume;
				}
		for (double &mass : profile.cumulative)
		{
			profile.total += mass;
			mass = profile.total;
		}
		return profile;
	}
} // namespace DefectStudio
