#include "Core/dspch.hpp"
#include "Domain/Electronic/HydrogenicOrbital.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] double LogFactorial(int value)
		{
			double result = 0.0;
			for (int factor = 2; factor <= value; ++factor)
				result += std::log(static_cast<double>(factor));
			return result;
		}

		[[nodiscard]] double AssociatedLaguerre(int degree, int alpha, double x)
		{
			if (degree == 0)
				return 1.0;

			double previous = 1.0;
			double current = 1.0 + static_cast<double>(alpha) - x;
			for (int order = 2; order <= degree; ++order)
			{
				const double next =
					((2.0 * order - 1.0 + alpha - x) * current -
						(order - 1.0 + alpha) * previous) /
					static_cast<double>(order);
				previous = current;
				current = next;
			}
			return current;
		}
	} // namespace

	float HydrogenicRadial(int n, int l, float effectiveCharge, float radius)
	{
		if (n < 1 || l < 0 || l >= n || effectiveCharge <= 0.0f || radius < 0.0f ||
			!std::isfinite(effectiveCharge) || !std::isfinite(radius))
		{
			return 0.0f;
		}

		const double scaledRadius =
			2.0 * static_cast<double>(effectiveCharge) * radius /
			(static_cast<double>(n) * kBohrRadiusAngstrom);
		const double inverseLength =
			2.0 * static_cast<double>(effectiveCharge) /
			(static_cast<double>(n) * kBohrRadiusAngstrom);
		const double logNormalisation = 0.5 *
			(3.0 * std::log(inverseLength) + LogFactorial(n - l - 1) -
				std::log(2.0 * n) - LogFactorial(n + l));
		const double value = std::exp(logNormalisation - 0.5 * scaledRadius) *
			std::pow(scaledRadius, l) * AssociatedLaguerre(n - l - 1, 2 * l + 1, scaledRadius);
		return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
	}

	float RealSphericalHarmonic(int l, int m, const glm::vec3 &offset)
	{
		if (l < 0 || std::abs(m) > l || l > 2)
			return 0.0f;

		constexpr float pi = std::numbers::pi_v<float>;
		if (l == 0)
			return std::sqrt(1.0f / (4.0f * pi));

		const float radiusSquared = glm::dot(offset, offset);
		if (radiusSquared <= 0.0f || !std::isfinite(radiusSquared))
			return 0.0f;

		const float inverseRadius = 1.0f / std::sqrt(radiusSquared);
		const float x = offset.x * inverseRadius;
		const float y = offset.y * inverseRadius;
		const float z = offset.z * inverseRadius;
		if (l == 1)
		{
			const float scale = std::sqrt(3.0f / (4.0f * pi));
			switch (m)
			{
				case -1: return scale * y;
				case 0: return scale * z;
				case 1: return scale * x;
				default: return 0.0f;
			}
		}

		switch (m)
		{
			case -2: return std::sqrt(15.0f / (4.0f * pi)) * x * y;
			case -1: return std::sqrt(15.0f / (4.0f * pi)) * y * z;
			case 0: return std::sqrt(5.0f / (16.0f * pi)) * (3.0f * z * z - 1.0f);
			case 1: return std::sqrt(15.0f / (4.0f * pi)) * x * z;
			case 2: return std::sqrt(15.0f / (16.0f * pi)) * (x * x - y * y);
			default: return 0.0f;
		}
	}

	float EvaluateAtomicOrbital(const AtomicOrbital &orbital, const glm::vec3 &offset)
	{
		if (orbital.n < 1 || orbital.l < 0 || orbital.l >= orbital.n ||
			std::abs(orbital.m) > orbital.l)
		{
			return 0.0f;
		}

		const float radius = glm::length(offset);
		return HydrogenicRadial(orbital.n, orbital.l, orbital.effectiveCharge, radius) *
			RealSphericalHarmonic(orbital.l, orbital.m, offset);
	}

	float EvaluateOrbital(const OrbitalWavefunction &wavefunction, const glm::vec3 &point)
	{
		float value = 0.0f;
		for (const OrbitalTerm &term : wavefunction.terms)
		{
			const glm::vec3 localOffset = glm::transpose(term.orientation) * (point - term.center);
			value += term.coefficient * EvaluateAtomicOrbital(term.orbital, localOffset);
		}
		return value;
	}

	glm::vec3 OrbitalCentroid(const OrbitalWavefunction &wavefunction)
	{
		if (wavefunction.terms.empty())
			return glm::vec3(0.0f);

		glm::vec3 sum(0.0f);
		for (const OrbitalTerm &term : wavefunction.terms)
			sum += term.center;
		return sum / static_cast<float>(wavefunction.terms.size());
	}

	float SuggestOrbitalExtent(const OrbitalWavefunction &wavefunction)
	{
		if (wavefunction.terms.empty())
			return 0.0f;

		const glm::vec3 centroid = OrbitalCentroid(wavefunction);
		float extent = 0.0f;
		for (const OrbitalTerm &term : wavefunction.terms)
		{
			if (term.orbital.n < 1 || term.orbital.effectiveCharge <= 0.0f)
				continue;
			const float n = static_cast<float>(term.orbital.n);
			const float tailRadius = 6.0f * n * n * kBohrRadiusAngstrom / term.orbital.effectiveCharge;
			extent = std::max(extent, glm::length(term.center - centroid) + tailRadius);
		}
		return extent;
	}

	OrbitalGridData SampleOrbitalToGrid(
		const OrbitalWavefunction &wavefunction, const OrbitalSamplingSettings &settings)
	{
		OrbitalGridData grid;
		if (wavefunction.terms.empty() || settings.dimensions.x < 2 || settings.dimensions.y < 2 ||
			settings.dimensions.z < 2)
		{
			return grid;
		}

		const float extent = settings.extent > 0.0f ? settings.extent : SuggestOrbitalExtent(wavefunction);
		if (extent <= 0.0f || !std::isfinite(extent))
			return grid;

		grid.dimensions = settings.dimensions;
		grid.cell = glm::mat3(2.0f * extent);
		grid.origin = OrbitalCentroid(wavefunction) - glm::vec3(extent);
		grid.values.resize(
			static_cast<std::size_t>(grid.dimensions.x) * static_cast<std::size_t>(grid.dimensions.y) *
			static_cast<std::size_t>(grid.dimensions.z));

		for (int x = 0; x < grid.dimensions.x; ++x)
		{
			const float fractionX = static_cast<float>(x) / static_cast<float>(grid.dimensions.x - 1);
			for (int y = 0; y < grid.dimensions.y; ++y)
			{
				const float fractionY = static_cast<float>(y) / static_cast<float>(grid.dimensions.y - 1);
				for (int z = 0; z < grid.dimensions.z; ++z)
				{
					const float fractionZ = static_cast<float>(z) / static_cast<float>(grid.dimensions.z - 1);
					const glm::vec3 point = grid.origin + grid.cell[0] * fractionX +
						grid.cell[1] * fractionY + grid.cell[2] * fractionZ;
					const std::size_t index =
						(static_cast<std::size_t>(x) * grid.dimensions.y + y) * grid.dimensions.z + z;
					grid.values[index] = EvaluateOrbital(wavefunction, point);
				}
			}
		}
		return grid;
	}

	float SuggestOrbitalIsoValue(const OrbitalGridData &grid, float fractionOfPeak)
	{
		float peak = 0.0f;
		for (const float value : grid.values)
			peak = std::max(peak, std::abs(value));
		return peak * std::max(fractionOfPeak, 0.0f);
	}
} // namespace DefectStudio
