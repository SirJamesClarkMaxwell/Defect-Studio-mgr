#pragma once

#include <glm/glm.hpp>

namespace DefectStudio
{
	// How the first frame of a path is chosen. Everything after it is transported, never re-seeded -
	// re-seeding per sample is what makes a swept tube spin when the tangent passes near world up.
	struct FrameSeed
	{
		enum class Mode
		{
			Auto,        // stable world-up fallback, or the arc plane normal when the caller supplies one
			FixedNormal, // project the given normal into the plane perpendicular to the tangent
		};

		Mode mode = Mode::Auto;
		glm::dvec3 normal{0.0, 0.0, 1.0}; // read only for FixedNormal, and as the Auto hint when finite
	};

	// An orthonormal right-handed frame: binormal == cross(tangent, normal).
	struct PathFrame
	{
		glm::dvec3 tangent{0.0, 0.0, 1.0};
		glm::dvec3 normal{1.0, 0.0, 0.0};
		glm::dvec3 binormal{0.0, 1.0, 0.0};
	};

	// First frame of a path. A zero, non-finite or degenerate tangent yields the identity frame rather
	// than NaN; a seed normal parallel to the tangent falls back to Auto instead of collapsing.
	[[nodiscard]] PathFrame SeedFrame(glm::dvec3 tangent, const FrameSeed &seed);

	// Double-reflection parallel transport of `previous` onto `tangent`: minimal rotation, no twist
	// accumulated, no flip when the tangent turns through or past world up. Returns `previous` unchanged
	// for a degenerate tangent so a bad sample cannot poison the rest of the path.
	[[nodiscard]] PathFrame TransportFrame(const PathFrame &previous, glm::dvec3 tangent);
}
