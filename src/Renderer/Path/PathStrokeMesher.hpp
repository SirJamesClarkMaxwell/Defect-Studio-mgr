#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathDecoration.hpp"
#include "Renderer/Path/PathStyle.hpp"
#include "Renderer/Path/PathTessellator.hpp"

namespace DefectStudio
{
	// Round profile: a finished tube. Nothing here depends on the camera.
	struct StrokeTubeVertex
	{
		glm::vec3 position{0.0f};
		glm::vec3 normal{0.0f, 0.0f, 1.0f};
		glm::vec4 color{1.0f};  // gradient already sampled per vertex
		float arcT = 0.0f;      // normalised arc length along the whole path
		float dashCoord = 0.0f; // world arc length, so the shader can discard by dash pattern
	};

	// Flat / CameraFacing profile: a centreline ribbon expanded in the shader. `side` is the expansion
	// sign, `tangent` the direction to expand perpendicular to.
	struct StrokeRibbonVertex
	{
		glm::vec3 position{0.0f};
		glm::vec3 tangent{0.0f, 0.0f, 1.0f};
		glm::vec3 normal{0.0f, 1.0f, 0.0f}; // ribbon plane normal; unused by CameraFacing
		glm::vec4 color{1.0f};
		float side = 0.0f; // -1 or +1
		float arcT = 0.0f;
		float dashCoord = 0.0f;
		float halfWidth = 0.0f; // CameraFacing expansion width; ignored by Flat
	};

	// Where one logical piece lives inside the mesh, so a test can assert "the end decoration produced
	// closed geometry" without re-deriving index arithmetic.
	struct StrokeMeshRange
	{
		std::uint32_t firstIndex = 0;
		std::uint32_t indexCount = 0;

		[[nodiscard]] bool IsEmpty() const
		{
			return indexCount == 0;
		}
	};

	// Exactly one of `tubeVertices` / `ribbonVertices` is populated, decided by the style profile.
	// `indices` addresses whichever one it is - triangle list, counter-clockwise front faces.
	struct StrokeGeometry
	{
		std::vector<StrokeTubeVertex> tubeVertices;
		std::vector<StrokeRibbonVertex> ribbonVertices;
		std::vector<std::uint32_t> indices;
		StrokeMeshRange shaft;
		StrokeMeshRange startDecoration;
		StrokeMeshRange endDecoration;
		ShaftRange shaftRange;      // arc-length span the shaft actually covers, after trimming
		double dashedLength = 0.0;  // total "on" length drawn; == shaft span when not dashed
		std::vector<PathDiagnostic> diagnostics;
	};

	// Samples the gradient at a normalised arc position. A disabled or empty gradient returns the flat
	// style colour with its alpha; otherwise the stops are interpolated in RGB and alpha, clamped at
	// both ends (no extrapolation past the first or last stop).
	[[nodiscard]] glm::vec4 SampleStrokeColor(const PathStrokeStyle &style, double normalizedT);

	// Builds the whole drawable for one path: shaft (dashed into runs if the style says so) plus both
	// endpoint decorations, in the profile the style names. Contract:
	// - Camera-independent: the same path and style produce byte-identical output from any viewpoint.
	//   CameraFacing defers its expansion to the shader precisely so that stays true.
	// - Caps and bends use the transported frames from the tessellator, so a bend of 0, 90 or 180 degrees
	//   all yield finite unit normals - no cross product of parallel vectors reaches a vertex.
	// - `style.join` does not change the Round profile: a ring per sample stitched to its neighbour is
	//   closed at a bend whatever the join, so Bevel and Round would produce identical tubes. It becomes
	//   load-bearing in S7, where the ribbon profiles are expanded in the shader and the join decides
	//   what fills the wedge at a bend. Deliberately not anticipated here with geometry no shader reads.
	// - A non-positive or non-finite width, fewer than 3 radial segments, or a malformed gradient
	//   (non-finite, out-of-range or decreasing stop positions) emits InvalidStrokeStyle /
	//   InvalidGradient and returns no geometry.
	// - Decorations longer than the path emit DecorationsExceedPathLength: the decorations are still
	//   built, the shaft is not.
	// - task/41 S11h, the decoration/shaft handoff. A decoration is RIGID: it is built in the frame of
	//   the path's endpoint and does not bend along the path, because an arrowhead is a solid object
	//   and a curved one looks worse than a detached one. The shaft is what gives way. So:
	//   * The shaft's boundary on a decorated end sits where the decoration's back sits, rather than
	//     wherever `AtLength(totalLength - trim)` happens to sample. Trimming by arc length lands on
	//     the curve; the decoration's back sits on the endpoint tangent; on an Arc or a Cubic those
	//     are two different places with two different frames, and the visible result was a head
	//     sitting in a plane of its own while the shaft twisted away from it.
	//   * What is shared is the POSITION and the FRAME, and nothing else. The shaft keeps its own
	//     width: a decoration is wider than the stroke it terminates, so a shaft that adopted the
	//     decoration's half-width would flare into a funnel over its last segment, and a Round tube
	//     would balloon to the arrowhead's back radius just before reaching it. The two pieces meet
	//     at a T-junction, not in a continuous surface.
	//   * Concretely, for all three profiles: the shaft's boundary sample is moved to the
	//     decoration's back centreline position and given the endpoint's tangent, normal and
	//     binormal. Its vertices are then built from that sample exactly as any other shaft sample
	//     is. The decoration's own vertices are NOT reused by the shaft.
	//   * A decorated end whose decoration closes its back gets NO shaft cap: the decoration already
	//     closes the tube, and adding a hemisphere there puts two surfaces in the same place. This is
	//     latent rather than observed - `cap` defaults to Butt and nothing in the UI sets it - but it
	//     is the same handoff and belongs with it.
	//   * An unfilled decoration does not close its back, so its end keeps its cap.
	// - task/41 S11m, a Flat stroke with `style.ribbonThickness > 0`. Its cross-section is a
	//   `width` by `ribbonThickness` rectangle, so it is built the way Round is built - rings of
	//   vertices stitched to their neighbours, into `tubeVertices` - with a four-corner rectangular
	//   ring in place of Round's circular one. The corners are
	//   `sample.position +/- (width/2) * normal +/- (ribbonThickness/2) * binormal`.
	//   * Everything downstream already works on rings: the joins, the caps, the decoration meshing
	//     and the S11h decoration/shaft handoff need no special case for it, and must not grow one.
	//   * The decorations extrude the same way. A thick ribbon's arrowhead is a solid of the same
	//     thickness, not a sheet glued to a solid shaft.
	//   * `style.radialSegments` does not apply: a rectangle has four corners.
	//   * Zero thickness keeps the existing shader-expanded sheet in `ribbonVertices`, unchanged.
	//     This is the default, so no existing path changes shape.
	// - Which vertex array is populated therefore no longer follows from the profile alone, and
	//   callers must key off the array rather than off `style.profile`. The invariant that exactly
	//   one of the two is populated still holds.
	// - task/41 S11q, gradient sampling. The stroke's colour is evaluated per vertex, and the
	//   tessellator subdivides on GEOMETRY alone - a straight Line is exact at two samples, so a
	//   three-stop gradient on one came out as a blend between the first stop and the last, with the
	//   middle stop never sampled. It showed on a dashed line and not on a solid one, because dashes
	//   cut the shaft into runs and each run brought its own vertices.
	//   * When `style.gradient` is enabled, the shaft is additionally subdivided at every stop's arc
	//     position, so every stop is a real vertex. The extra samples lie on the curve: colour
	//     resolution changes, shape does not.
	//   * Two stops sharing a position produce two coincident boundary samples, so the colour steps
	//     rather than blending. That is what a hard colour edge means, and the ramp can author it.
	//   * A disabled or empty gradient adds no samples, so nothing without a gradient changes.
	// - Fewer than two samples, or a zero-length path, yields empty geometry and no crash.
	// - No NaN or Inf ever reaches a vertex field.
	[[nodiscard]] StrokeGeometry BuildStroke(const EvaluatedPath &evaluated, const PathStrokeStyle &style);
}
