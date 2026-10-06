#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/IsosurfaceMesher.hpp"

namespace DefectStudio
{
	// task/51: the vacancy marker - the dashed circle a defect figure draws for a missing atom. A
	// disc in the plane through the vacancy that faces the camera, plus a ring of dashes on its rim.
	//
	// It is meshed into the isosurface overlay's triangle soup for the same reason a ScenePlane is
	// (OpenGlScenePlaneRenderer.cpp): that pipeline already draws translucent two-sided triangles
	// in a flat colour, so a marker needs no shader, no pipeline and no uniforms of its own. The
	// fill and the ring are separate soups because they are drawn with different alphas - the fill
	// at RendererVacancyData::opacity, the ring opaque.
	struct VacancyMarkerMesh
	{
		// Flat GL_TRIANGLES triplets, sign +1. Empty for VacancyRenderMode::Wireframe.
		std::vector<IsosurfaceVertex> fill;
		// Flat GL_TRIANGLES triplets, sign -1. Never empty for a valid marker - every mode has a ring.
		std::vector<IsosurfaceVertex> ring;
	};

	// Segments of the fill's triangle fan: exactly this many triangles, all sharing the centre.
	inline constexpr int kVacancyDiscSegments = 48;
	// Quads per dash (each quad two triangles), so a dash follows the arc instead of cutting a chord.
	inline constexpr int kVacancyDashSubdivisions = 4;
	// Quads of a continuous ring (dashCount == 0).
	inline constexpr int kVacancySolidRingSegments = 48;
	inline constexpr int kVacancyMaxDashCount = 64;

	// The plane is spanned by `cameraRight` and `cameraUp` (both unit, perpendicular - the camera's
	// own basis); every vertex's normal is normalize(cross(cameraRight, cameraUp)), facing the viewer.
	//
	// Geometry, with r = vacancy.radius, w = min(ringWidth, r), centre = vacancy.cartesianPosition:
	// - fill: kVacancyDiscSegments triangles (centre, rim_i, rim_i+1), rim on radius r, so the
	//   entire interior, including the gaps between dashes, occludes/tints bonds.
	// - ring: an annulus between r - w and r. dashCount = clamp(vacancy.dashCount, 0,
	//   kVacancyMaxDashCount). dashCount > 0: dashCount dashes, dash k covering the angles
	//   [k, k + 0.5] * 2pi / dashCount measured from cameraRight toward cameraUp (a 50% duty cycle),
	//   each kVacancyDashSubdivisions quads. dashCount == 0: one continuous ring of
	//   kVacancySolidRingSegments quads.
	//
	// `ringWidth` is the caller's: the style's RendererVacancyData::ringWidth after the backend's
	// screen-space floor (the ScenePlaneBorderWidth rule), so a far-away marker keeps a visible ring.
	// Returns two empty soups when radius <= 0 or ringWidth <= 0.
	[[nodiscard]] VacancyMarkerMesh BuildVacancyMarkerMesh(
		const RendererVacancyData &vacancy,
		const glm::vec3 &cameraRight,
		const glm::vec3 &cameraUp,
		float ringWidth);
} // namespace DefectStudio
