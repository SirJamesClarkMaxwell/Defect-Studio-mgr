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
	// - Fewer than two samples, or a zero-length path, yields empty geometry and no crash.
	// - No NaN or Inf ever reaches a vertex field.
	[[nodiscard]] StrokeGeometry BuildStroke(const EvaluatedPath &evaluated, const PathStrokeStyle &style);
}
