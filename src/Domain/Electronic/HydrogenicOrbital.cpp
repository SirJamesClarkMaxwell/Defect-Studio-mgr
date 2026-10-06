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
		if (l < 0 || std::abs(m) > l || l > 3)
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

		if (l == 2)
		{
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

		switch (m)
		{
			case -3: return std::sqrt(35.0f / (32.0f * pi)) * y * (3.0f * x * x - y * y);
			case -2: return std::sqrt(105.0f / (4.0f * pi)) * x * y * z;
			case -1: return std::sqrt(21.0f / (32.0f * pi)) * y * (5.0f * z * z - 1.0f);
			case 0: return std::sqrt(7.0f / (16.0f * pi)) * (5.0f * z * z * z - 3.0f * z);
			case 1: return std::sqrt(21.0f / (32.0f * pi)) * x * (5.0f * z * z - 1.0f);
			case 2: return std::sqrt(105.0f / (16.0f * pi)) * z * (x * x - y * y);
			case 3: return std::sqrt(35.0f / (32.0f * pi)) * x * (x * x - 3.0f * y * y);
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

		// Measured, not derived from a formula. The obvious analytic choice - the classical outer
		// turning point, 6 n^2 a0 / Z - overshoots badly: for a 2sp hybrid at Z = 1 it asks for a
		// 25 A box around a lobe under 4 A wide, so at the default 48 samples per axis the orbital
		// gets about a dozen samples across itself and meshes into a faceted blob. Sweeping
		// outwards and asking where the amplitude actually dies costs a few thousand evaluations
		// once per re-bake and gets the box within about twice the visible lobe.
		constexpr float kTailFraction = 0.02f;
		constexpr int kRadialSteps = 256;
		// A Fibonacci sphere rather than the axes and cube diagonals. Those coincide exactly with
		// the nodal planes of the very orbitals that need measuring - a delta* probed along +-x,
		// +-y, +-z and the body diagonals reads as identically zero and the box collapses to
		// nothing. An irrational-angle spiral cannot line up with any of these symmetries.
		static const std::vector<glm::vec3> kProbeDirections = [] {
			constexpr int kCount = 64;
			const float golden = std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
			std::vector<glm::vec3> directions;
			directions.reserve(kCount);
			for (int index = 0; index < kCount; ++index)
			{
				const float z = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / static_cast<float>(kCount);
				const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
				const float theta = golden * static_cast<float>(index);
				directions.push_back({radius * std::cos(theta), radius * std::sin(theta), z});
			}
			return directions;
		}();

		const glm::vec3 centroid = OrbitalCentroid(wavefunction);
		// Far enough out that the sweep is guaranteed to pass the tail of the most diffuse term,
		// plus whatever spread the centres themselves have.
		float ceiling = 0.0f;
		for (const OrbitalTerm &term : wavefunction.terms)
		{
			if (term.orbital.n < 1 || term.orbital.effectiveCharge <= 0.0f)
				continue;
			const float n = static_cast<float>(term.orbital.n);
			ceiling = std::max(
				ceiling,
				glm::length(term.center - centroid) + 8.0f * n * n * kBohrRadiusAngstrom / term.orbital.effectiveCharge);
		}
		if (ceiling <= 0.0f)
			return 0.0f;

		const float step = ceiling / static_cast<float>(kRadialSteps);
		float peak = 0.0f;
		float extent = 0.0f;
		std::vector<float> shellPeaks(kRadialSteps + 1, 0.0f);
		for (int radialIndex = 0; radialIndex <= kRadialSteps; ++radialIndex)
		{
			const float radius = static_cast<float>(radialIndex) * step;
			float shellPeak = 0.0f;
			for (const glm::vec3 &direction : kProbeDirections)
				shellPeak = std::max(shellPeak, std::abs(EvaluateOrbital(wavefunction, centroid + direction * radius)));
			shellPeaks[static_cast<std::size_t>(radialIndex)] = shellPeak;
			peak = std::max(peak, shellPeak);
		}
		if (peak <= 0.0f)
			return 0.0f;

		for (int radialIndex = kRadialSteps; radialIndex >= 0; --radialIndex)
		{
			if (shellPeaks[static_cast<std::size_t>(radialIndex)] >= kTailFraction * peak)
			{
				extent = static_cast<float>(radialIndex) * step;
				break;
			}
		}
		// One step of slack so the outermost contour is not clipped by the box wall itself.
		return extent + step;
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
