#include "Core/dspch.hpp"

#include "Renderer/Path/PathFrames.hpp"

#include <cmath>

namespace DefectStudio
{
	namespace
	{
		constexpr double kEpsilon = 1.0e-12;

		[[nodiscard]] bool IsFinite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] glm::dvec3 NormaliseOr(glm::dvec3 value, glm::dvec3 fallback)
		{
			const double length = glm::length(value);
			return IsFinite(value) && std::isfinite(length) && length > kEpsilon ? value / length : fallback;
		}

		[[nodiscard]] PathFrame MakeFrame(glm::dvec3 tangent, glm::dvec3 normal)
		{
			tangent = NormaliseOr(tangent, glm::dvec3(0.0, 0.0, 1.0));
			normal -= tangent * glm::dot(normal, tangent);
			if (glm::length(normal) <= kEpsilon || !IsFinite(normal))
			{
				const glm::dvec3 axis = std::abs(tangent.z) < 0.9 ? glm::dvec3(0.0, 0.0, 1.0) : glm::dvec3(0.0, 1.0, 0.0);
				normal = axis - tangent * glm::dot(axis, tangent);
			}
			normal = NormaliseOr(normal, glm::dvec3(1.0, 0.0, 0.0));
			const glm::dvec3 binormal = NormaliseOr(glm::cross(tangent, normal), glm::dvec3(0.0, 1.0, 0.0));
			normal = NormaliseOr(glm::cross(binormal, tangent), glm::dvec3(1.0, 0.0, 0.0));
			return {tangent, normal, binormal};
		}
	} // namespace

	PathFrame SeedFrame(glm::dvec3 tangent, const FrameSeed &seed)
	{
		if (!IsFinite(tangent) || glm::length(tangent) <= kEpsilon)
			return {};
		const glm::dvec3 unitTangent = glm::normalize(tangent);
		if (seed.mode == FrameSeed::Mode::FixedNormal && IsFinite(seed.normal))
		{
			const glm::dvec3 projected = seed.normal - unitTangent * glm::dot(seed.normal, unitTangent);
			if (glm::length(projected) > kEpsilon)
				return MakeFrame(unitTangent, projected);
		}
		if (seed.mode == FrameSeed::Mode::Auto && IsFinite(seed.normal))
		{
			const glm::dvec3 projected = seed.normal - unitTangent * glm::dot(seed.normal, unitTangent);
			if (glm::length(projected) > kEpsilon)
				return MakeFrame(unitTangent, projected);
		}
		const glm::dvec3 up = std::abs(unitTangent.z) < 0.9 ? glm::dvec3(0.0, 0.0, 1.0) : glm::dvec3(0.0, 1.0, 0.0);
		return MakeFrame(unitTangent, up);
	}

	PathFrame TransportFrame(const PathFrame &previous, glm::dvec3 tangent)
	{
		if (!IsFinite(tangent) || glm::length(tangent) <= kEpsilon)
			return previous;
		const glm::dvec3 target = glm::normalize(tangent);
		const PathFrame source = MakeFrame(previous.tangent, previous.normal);
		if (glm::dot(target - source.tangent, target - source.tangent) <= kEpsilon)
			return MakeFrame(target, source.normal);

		// Two reflections compose into the minimal rotation that carries source.tangent onto target:
		// the first across the plane bisecting them (tangent -> -target), the second across the plane
		// of the target (-target -> target). One reflection alone mirrors the frame instead of
		// rotating it, which reads as a normal flipping sign at every sample.
		glm::dvec3 normal = source.normal;
		const glm::dvec3 bisector = source.tangent + target;
		const double bisectorSquared = glm::dot(bisector, bisector);
		if (bisectorSquared > kEpsilon)
			normal -= 2.0 * glm::dot(normal, bisector) / bisectorSquared * bisector;
		normal -= 2.0 * glm::dot(normal, target) * target;
		return MakeFrame(target, normal);
	}
} // namespace DefectStudio
