#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathFrames.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	struct TessellationSettings
	{
		double worldTolerance = 0.01; // max chord-height deviation from the true curve
		int maxDepth = 12;
		int maxSamplesPerSegment = 4096;
		FrameSeed frameSeed{};
	};

	struct EvaluatedSample
	{
		glm::dvec3 position{0.0};
		glm::dvec3 tangent{0.0};
		glm::dvec3 normal{0.0};
		glm::dvec3 binormal{0.0};
		PathElementId segment;    // the segment this sample belongs to; a shared node carries the segment it ends
		double localT = 0.0;      // [0, 1] within that segment
		double arcLength = 0.0;   // cumulative from the path start
		double normalizedT = 0.0; // arcLength / totalLength, or 0 for a zero-length path
	};

	struct EvaluatedPath
	{
		std::vector<EvaluatedSample> samples;
		std::vector<PathDiagnostic> diagnostics;
		double totalLength = 0.0;
	};

	// Adaptive subdivision on chord-height error, with parallel-transported frames carried across segment
	// boundaries. Contract:
	// - Node positions are shared: a sample sits on each interior node exactly once, never twice.
	// - Line segments emit their two endpoints and nothing else.
	// - Hitting maxDepth or maxSamplesPerSegment emits a diagnostic (TessellationDepthLimit /
	//   TessellationSampleLimit) and returns the best samples reached - never silently exceeds tolerance.
	// - An invalid path (ValidatePath non-empty), a degenerate arc or non-finite settings yields the
	//   corresponding diagnostics and no samples. No NaN ever reaches a sample field.
	// - samples.back().arcLength == totalLength, within tolerance of CumulativeLengths().back().
	[[nodiscard]] EvaluatedPath Tessellate(const ScenePath &path, const ResolvedNodes &resolved, const TessellationSettings &settings);
}
