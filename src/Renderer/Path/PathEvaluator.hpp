#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	enum class PathDiagnosticCode
	{
		NodeCountMismatch,
		DuplicateElementId,
		NonFinite,
		ZeroChord,
		ArcNormalParallelToChord,
		ArcSweepOutOfRange,
		ArcNonFiniteDerived,
		BrokenBinding,
		ObjectOriginTargetsPath,
		InteriorNodeBuffer,
		// Topology rejections (S2). The model has no repair strategy for these - they are reported,
		// never guessed around.
		InvalidSegmentIndex,
		ParameterOutOfRange,
		UnknownElement,
		MergeRequiresLineNeighbours,
		LastNodeNotRemovable,
		TangentNotApplicable,
		// Tessellation limits (S3). Reported, not hidden: the caller decides whether a coarser curve is
		// acceptable or the tolerance was unreasonable.
		TessellationDepthLimit,
		TessellationSampleLimit,
		InvalidTessellationSettings,
		// Stroke and decoration rejections (S5). A style that cannot produce geometry is reported once;
		// the mesher then emits nothing rather than a degenerate strip the renderer would have to guess at.
		InvalidStrokeStyle,
		InvalidGradient,
		DecorationsExceedPathLength,
	};

	[[nodiscard]] const char *PathDiagnosticCodeName(PathDiagnosticCode code);

	struct PathDiagnostic
	{
		PathDiagnosticCode code = PathDiagnosticCode::NonFinite;
		PathElementId element; // invalid id == the diagnostic is about the path as a whole
		std::string message;
	};

	// Invariants: N nodes / N-1 segments, unique element ids, finite authored data, valid arcs.
	// Empty result == the path is structurally sound.
	[[nodiscard]] std::vector<PathDiagnostic> ValidatePath(const ScenePath &path);

	// Everything an arc segment needs that is NOT stored: derived in double from the two endpoints,
	// the plane normal and the signed sweep.
	struct ArcGeometry
	{
		glm::dvec3 center{0.0};
		glm::dvec3 normal{0.0, 0.0, 1.0}; // unit, re-orthogonalised against the chord
		glm::dvec3 startDirection{1.0, 0.0, 0.0}; // unit, center -> start endpoint
		double radius = 0.0;
		double startAngle = 0.0;
		double signedSweep = 0.0;
	};

	// r = |AB| / (2 sin(|theta|/2)); rejects zero chord, non-finite input, a normal parallel to the
	// chord and |theta| outside [epsilon, 2*pi - epsilon]. Never returns NaN in a value.
	[[nodiscard]] Result<ArcGeometry> DeriveArc(glm::dvec3 a, glm::dvec3 b, glm::vec3 normal, float signedSweep);

	struct PathSample
	{
		glm::dvec3 position{0.0};
		glm::dvec3 tangent{0.0}; // unit, in the direction of travel
	};

	// Node positions after binding resolution; index-parallel with ScenePath::nodes.
	struct ResolvedNodes
	{
		std::vector<glm::vec3> positions;
		std::vector<PathDiagnostic> diagnostics;
	};

	// t in [0, 1] along one segment. Out-of-range t, a bad segment index or invalid geometry is an
	// error, never a clamped guess.
	[[nodiscard]] Result<PathSample> EvaluateSegment(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment, double t);

	// Arc length. Line = chord, arc = r * |theta|, cubic = bounded-error Gauss-Legendre with
	// subdivision (documented error bound, not a sample sum).
	[[nodiscard]] Result<double> SegmentLength(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment);

	// Inverse of SegmentLength: the t whose arc length from the segment start equals s.
	[[nodiscard]] Result<double> SegmentParamAtLength(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment, double s);

	// size == segments + 1; [0] == 0, back() == total length. Empty on an invalid path.
	[[nodiscard]] std::vector<double> CumulativeLengths(const ScenePath &path, const ResolvedNodes &resolved);
}
