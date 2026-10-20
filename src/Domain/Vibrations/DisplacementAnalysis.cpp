#include "Core/dspch.hpp"
#include "Domain/Vibrations/DisplacementAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		std::vector<double> Weights(const CcdDisplacement &displacement)
		{
			const auto count = displacement.symbols.size();
			if (count == 0 || displacement.masses.size() != count || displacement.displacement.size() != count)
				return {};
			std::vector<double> weights;
			for (std::size_t i = 0; i < count; ++i)
			{
				const double mass = displacement.masses[i];
				const auto &vector = displacement.displacement[i];
				const double weight = mass * glm::dot(vector, vector);
				if (!std::isfinite(mass) || mass <= 0.0 || !Finite(vector) || !std::isfinite(weight))
					return {};
				weights.push_back(weight);
			}
			const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
			return total > 0.0 && std::isfinite(total) ? weights : std::vector<double>{};
		}

		glm::dvec3 MinimumImage(const glm::dmat3 &cell, const glm::dmat3 &inverse, const glm::dvec3 &delta)
		{
			glm::dvec3 fractional = inverse * delta;
			fractional -= glm::round(fractional);
			glm::dvec3 best = cell * fractional;
			double bestSquared = glm::dot(best, best);
			const double radius = std::sqrt(bestSquared);
			glm::dvec3 lower, upper;
			// Inverse-cell row norms bound every image closer than the initial candidate, even in skewed cells.
			for (int axis = 0; axis < 3; ++axis)
			{
				const double bound = radius * glm::length(glm::dvec3(inverse[0][axis], inverse[1][axis], inverse[2][axis]));
				lower[axis] = std::ceil(fractional[axis] - bound);
				upper[axis] = std::floor(fractional[axis] + bound);
			}
			for (double x = lower.x; x <= upper.x; ++x)
				for (double y = lower.y; y <= upper.y; ++y)
					for (double z = lower.z; z <= upper.z; ++z)
					{
						const auto candidate = cell * (fractional - glm::dvec3(x, y, z));
						const double squared = glm::dot(candidate, candidate);
						if (squared < bestSquared)
						{
							best = candidate;
							bestSquared = squared;
						}
					}
			return best;
		}
	}

	std::optional<glm::dvec3> DisplacementCentre(const CcdDisplacement &displacement)
	{
		const auto weights = Weights(displacement);
		if (weights.empty() || displacement.gsPositions.size() != weights.size())
			return std::nullopt;
		const glm::dmat3 cell(displacement.cell[0], displacement.cell[1], displacement.cell[2]);
		const double determinant = glm::determinant(cell);
		if (!Finite(cell[0]) || !Finite(cell[1]) || !Finite(cell[2]) || !std::isfinite(determinant) || determinant == 0.0 ||
			std::any_of(displacement.gsPositions.begin(), displacement.gsPositions.end(), [](const auto &position) { return !Finite(position); }))
			return std::nullopt;
		const auto inverse = glm::inverse(cell);
		const auto anchor = displacement.gsPositions[std::max_element(weights.begin(), weights.end()) - weights.begin()];
		glm::dvec3 offset(0.0);
		const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
		for (std::size_t i = 0; i < weights.size(); ++i)
			offset += (weights[i] / total) * MinimumImage(cell, inverse, displacement.gsPositions[i] - anchor);
		const auto fractional = inverse * (anchor + offset);
		return cell * (fractional - glm::floor(fractional));
	}

	std::vector<std::pair<double, double>> LocalisationCurve(const CcdDisplacement &displacement)
	{
		const auto centre = DisplacementCentre(displacement);
		if (!centre)
			return {};
		const auto weights = Weights(displacement);
		const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
		const glm::dmat3 cell(displacement.cell[0], displacement.cell[1], displacement.cell[2]);
		const auto inverse = glm::inverse(cell);
		std::vector<std::pair<double, double>> curve;
		for (std::size_t i = 0; i < weights.size(); ++i)
			curve.emplace_back(glm::length(MinimumImage(cell, inverse, displacement.gsPositions[i] - *centre)), weights[i] / total);
		std::sort(curve.begin(), curve.end());
		double cumulative = 0.0;
		for (auto &[radius, share] : curve)
		{
			cumulative += share;
			share = std::min(cumulative, 1.0);
		}
		curve.back().second = 1.0;
		return curve;
	}

	std::size_t AtomsCarrying(const CcdDisplacement &displacement, double fraction)
	{
		if (!std::isfinite(fraction) || fraction <= 0.0 || fraction > 1.0)
			return 0;
		auto weights = Weights(displacement);
		const double target = fraction * std::accumulate(weights.begin(), weights.end(), 0.0);
		std::sort(weights.begin(), weights.end(), std::greater<double>());
		double sum = 0.0;
		for (std::size_t i = 0; i < weights.size(); ++i)
		{
			sum += weights[i];
			if (sum >= target)
				return i + 1;
		}
		return weights.size();
	}

	std::optional<double> DisplacementOverlap(const CcdDisplacement &a, const CcdDisplacement &b)
	{
		if (a.symbols != b.symbols || Weights(a).empty() || Weights(b).empty() ||
			!std::isfinite(a.deltaQ) || !std::isfinite(b.deltaQ) || a.deltaQ <= 0.0 || b.deltaQ <= 0.0)
			return std::nullopt;
		double dot = 0.0;
		for (std::size_t i = 0; i < a.symbols.size(); ++i)
		{
			if (std::abs(a.masses[i] - b.masses[i]) > 1e-3)
				return std::nullopt;
			dot += a.masses[i] * glm::dot(a.displacement[i], b.displacement[i]);
		}
		const double overlap = std::abs(dot / a.deltaQ / b.deltaQ);
		return std::isfinite(overlap) ? std::optional<double>(std::clamp(overlap, 0.0, 1.0)) : std::nullopt;
	}
}
