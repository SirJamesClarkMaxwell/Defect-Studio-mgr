#pragma once

#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::detail
{
	// The seam between PathStrokeMesher.cpp and PathDecorationMesher.cpp. The two were one file
	// until S11i's hollow bodies pushed it past the size limit, and they still share three pieces:
	// the shaft mesher needs the decoration appended, and the decoration mesher needs the shaft's
	// ring stitching. `detail` rather than a public header because nothing outside those two
	// translation units has any business calling these - BuildStroke is the whole public surface.

	// Which contour point is the decoration's back. Not simply the last one: a contour may end with
	// several points at the same `s` (an Arrow ends at both (length, width) and (length, 0)), and
	// the back is the widest of them, because that is the ring the shaft has to meet.
	[[nodiscard]] std::size_t BackContourPoint(const DecorationContour &contour);

	// Appends one cross-section ring and returns its first vertex. Round uses the configured radial
	// count; a thick Flat stroke uses four rectangular corners. Shaft and decoration meshing both use
	// this seam so the shape of a ring cannot diverge between them.
	std::uint32_t AppendCrossSectionRing(
		StrokeGeometry &geometry, const glm::dvec3 &centre, const EvaluatedSample &sample,
		const double halfWidth, const PathStrokeStyle &style, const bool inner = false,
		const double scale = 1.0);

	// task/41 S11p: a contour point of halfWidth 0 closes a CIRCULAR cross-section and does not
	// close any other. For Round the ring collapses to a single point on the axis, so the band to
	// its neighbour is a fan and the solid is sealed. For the rectangular ring of a thick Flat
	// stroke the same point still has thickness, so the ring is a line segment: the band degenerates
	// on two of its four sides and leaves a triangular hole there. An Arrow's contour starts and
	// ends at halfWidth 0, so a thick Flat arrowhead had a hole at the tip and another at the back,
	// and the back one is what the manual round saw as a dark notch where the shaft arrives.
	//
	// So a degenerate ring must be closed explicitly rather than by relying on the band. Both ends,
	// and for every non-circular cross-section.
	[[nodiscard]] bool UsesTubeVertices(const PathStrokeStyle &style);
	[[nodiscard]] std::uint32_t CrossSectionRingSize(const PathStrokeStyle &style);

	// One quad band between two rings of `radialSegments` vertices. Both ring starts are explicit
	// because a cap's rings are appended after the whole shaft rather than next to the ring they
	// attach to, so assuming `upper == lower + radialSegments` stitches the wrong pair.
	void StitchRings(StrokeGeometry &geometry, std::uint32_t lower, std::uint32_t upper,
		std::uint32_t radialSegments, bool flip = false);

	// Meshes one endpoint decoration in the endpoint's own frame, revolved for Round and mirrored
	// for the ribbon profiles, hollow when the contour says so. Writes the index span it produced
	// into `range`.
	void AppendDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
		const EvaluatedSample &endpoint, bool start, const PathStrokeStyle &style,
		StrokeMeshRange &range);
}
