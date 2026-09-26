#include "Core/dspch.hpp"

#include "Renderer/Path/PathEvaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <unordered_set>

namespace DefectStudio
{
	namespace
	{
		constexpr double kEpsilon = 1.0e-12;
		constexpr double kArcSweepEpsilon = 1.0e-6;
		constexpr std::array<double, 5> kGaussNodes = {0.0, -0.5384693101056831, 0.5384693101056831, -0.9061798459386640, 0.9061798459386640};
		constexpr std::array<double, 5> kGaussWeights = {0.5688888888888889, 0.4786286704993665, 0.4786286704993665, 0.2369268850561891, 0.2369268850561891};

		[[nodiscard]] bool IsFinite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] bool IsFinite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] StructuredError MakeError(PathDiagnosticCode code, const char *detail)
		{
			return {ErrorCategory::Validation, Severity::Error, "Invalid path geometry.", detail,
				"Correct the path data and try again.", "Renderer/Path", PathDiagnosticCodeName(code), DisplayPolicy::Silent};
		}

		[[nodiscard]] Result<void> CheckSegment(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment)
		{
			if (segment >= path.segments.size() || segment + 1 >= resolved.positions.size())
				return MakeError(PathDiagnosticCode::NodeCountMismatch, "Segment index or resolved node count is invalid.");
			return {};
		}

		[[nodiscard]] glm::dvec3 CubicDerivative(
			glm::dvec3 c1, glm::dvec3 c2, glm::dvec3 a, glm::dvec3 b, double t)
		{
			const double u = 1.0 - t;
			return 3.0 * u * u * (c1 - a) + 6.0 * u * t * (c2 - c1) + 3.0 * t * t * (b - c2);
		}

		[[nodiscard]] double CubicSpeed(
			glm::dvec3 c1, glm::dvec3 c2, glm::dvec3 a, glm::dvec3 b, double t)
		{
			return glm::length(CubicDerivative(c1, c2, a, b, t));
		}

		[[nodiscard]] double GaussLength(
			glm::dvec3 c1, glm::dvec3 c2, glm::dvec3 a, glm::dvec3 b, double start, double end)
		{
			const double midpoint = (start + end) * 0.5;
			const double halfWidth = (end - start) * 0.5;
			double sum = 0.0;
			for (std::size_t index = 0; index < kGaussNodes.size(); ++index)
				sum += kGaussWeights[index] * CubicSpeed(c1, c2, a, b, midpoint + halfWidth * kGaussNodes[index]);
			return halfWidth * sum;
		}

		// Five-point Gauss-Legendre refinement has an explicit local error estimate: the difference
		// between one interval and its two children. This is integration, not a sum over curve samples.
		[[nodiscard]] double AdaptiveCubicLength(
			glm::dvec3 c1, glm::dvec3 c2, glm::dvec3 a, glm::dvec3 b,
			double start, double end, double tolerance, int depth)
		{
			const double whole = GaussLength(c1, c2, a, b, start, end);
			const double middle = (start + end) * 0.5;
			const double split = GaussLength(c1, c2, a, b, start, middle) + GaussLength(c1, c2, a, b, middle, end);
			if (depth == 0 || std::abs(split - whole) <= tolerance)
				return split;
			return AdaptiveCubicLength(c1, c2, a, b, start, middle, tolerance * 0.5, depth - 1) +
				AdaptiveCubicLength(c1, c2, a, b, middle, end, tolerance * 0.5, depth - 1);
		}

		[[nodiscard]] Result<double> CubicLength(
			glm::dvec3 c1, glm::dvec3 c2, glm::dvec3 a, glm::dvec3 b, double end = 1.0)
		{
			if (!IsFinite(a) || !IsFinite(b) || !IsFinite(c1) || !IsFinite(c2))
				return MakeError(PathDiagnosticCode::NonFinite, "Cubic control data is not finite.");
			return AdaptiveCubicLength(c1, c2, a, b, 0.0, end, 1.0e-12, 20);
		}
	} // namespace

	const char *PathDiagnosticCodeName(PathDiagnosticCode code)
	{
		switch (code)
		{
			case PathDiagnosticCode::NodeCountMismatch: return "path.node_count_mismatch";
			case PathDiagnosticCode::DuplicateElementId: return "path.duplicate_element_id";
			case PathDiagnosticCode::NonFinite: return "path.non_finite";
			case PathDiagnosticCode::ZeroChord: return "path.zero_chord";
			case PathDiagnosticCode::ArcNormalParallelToChord: return "path.arc_normal_parallel_to_chord";
			case PathDiagnosticCode::ArcSweepOutOfRange: return "path.arc_sweep_out_of_range";
			case PathDiagnosticCode::ArcNonFiniteDerived: return "path.arc_non_finite_derived";
			case PathDiagnosticCode::BrokenBinding: return "path.broken_binding";
			case PathDiagnosticCode::ObjectOriginTargetsPath: return "path.object_origin_targets_path";
			case PathDiagnosticCode::InteriorNodeBuffer: return "path.interior_node_buffer";
			case PathDiagnosticCode::InvalidSegmentIndex: return "path.invalid_segment_index";
			case PathDiagnosticCode::ParameterOutOfRange: return "path.parameter_out_of_range";
			case PathDiagnosticCode::UnknownElement: return "path.unknown_element";
			case PathDiagnosticCode::MergeRequiresLineNeighbours: return "path.merge_requires_line_neighbours";
			case PathDiagnosticCode::LastNodeNotRemovable: return "path.last_node_not_removable";
			case PathDiagnosticCode::TangentNotApplicable: return "path.tangent_not_applicable";
			case PathDiagnosticCode::TessellationDepthLimit: return "path.tessellation_depth_limit";
			case PathDiagnosticCode::TessellationSampleLimit: return "path.tessellation_sample_limit";
			case PathDiagnosticCode::InvalidTessellationSettings: return "path.invalid_tessellation_settings";
			case PathDiagnosticCode::InvalidStrokeStyle: return "path.invalid_stroke_style";
			case PathDiagnosticCode::InvalidGradient: return "path.invalid_gradient";
			case PathDiagnosticCode::DecorationsExceedPathLength: return "path.decorations_exceed_path_length";
		}
		return "path.unknown";
	}

	std::vector<PathDiagnostic> ValidatePath(const ScenePath &path)
	{
		std::vector<PathDiagnostic> diagnostics;
		if (path.segments.size() + 1 != path.nodes.size())
			diagnostics.push_back({PathDiagnosticCode::NodeCountMismatch, {}, "Open paths require N nodes and N-1 segments."});
		std::unordered_set<std::uint64_t> ids;
		auto checkId = [&](PathElementId id) {
			if (id.IsValid() && !ids.insert(id.value).second)
				diagnostics.push_back({PathDiagnosticCode::DuplicateElementId, id, "Path element ids must be unique."});
		};
		for (const PathNode &node : path.nodes)
		{
			checkId(node.id);
			if (!IsFinite(node.position))
				diagnostics.push_back({PathDiagnosticCode::NonFinite, node.id, "Node position is not finite."});
			std::visit([&](const auto &binding) {
				if constexpr (!std::is_same_v<std::decay_t<decltype(binding)>, PathBinding::Free>)
					if (!IsFinite(binding.offset))
						diagnostics.push_back({PathDiagnosticCode::NonFinite, node.id, "Binding offset is not finite."});
			}, node.binding.value);
		}
		for (std::size_t index = 0; index < path.segments.size(); ++index)
		{
			const PathSegment &segment = path.segments[index];
			checkId(segment.id);
			std::visit([&](const auto &data) {
				using Data = std::decay_t<decltype(data)>;
				if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
				{
					checkId(data.startHandle.id);
					checkId(data.endHandle.id);
					if (!IsFinite(data.startHandle.offset) || !IsFinite(data.endHandle.offset))
						diagnostics.push_back({PathDiagnosticCode::NonFinite, segment.id, "Cubic handle offset is not finite."});
				}
				else if constexpr (std::is_same_v<Data, CircularArcSegmentData>)
				{
					if (index + 1 < path.nodes.size())
					{
						const Result<ArcGeometry> arc = DeriveArc(glm::dvec3(path.nodes[index].position), glm::dvec3(path.nodes[index + 1].position), data.planeNormal, data.signedSweepRadians);
						if (!arc)
							diagnostics.push_back({PathDiagnosticCode::ArcNonFiniteDerived, segment.id, arc.Error().technicalDetails});
					}
				}
			}, segment.data);
		}
		return diagnostics;
	}

	Result<ArcGeometry> DeriveArc(glm::dvec3 a, glm::dvec3 b, glm::vec3 normal, float signedSweep)
	{
		if (!IsFinite(a) || !IsFinite(b) || !IsFinite(normal) || !std::isfinite(signedSweep))
			return MakeError(PathDiagnosticCode::NonFinite, "Arc input is not finite.");
		const glm::dvec3 chord = b - a;
		const double chordLength = glm::length(chord);
		if (chordLength <= kEpsilon)
			return MakeError(PathDiagnosticCode::ZeroChord, "Arc endpoints must be distinct.");
		const double magnitude = std::abs(static_cast<double>(signedSweep));
		if (magnitude < kArcSweepEpsilon || magnitude > 2.0 * std::numbers::pi_v<double> - kArcSweepEpsilon)
			return MakeError(PathDiagnosticCode::ArcSweepOutOfRange, "Arc sweep must be strictly between zero and a full turn.");
		const glm::dvec3 chordDirection = chord / chordLength;
		glm::dvec3 orthogonalNormal(normal);
		orthogonalNormal -= glm::dot(orthogonalNormal, chordDirection) * chordDirection;
		const double normalLength = glm::length(orthogonalNormal);
		if (normalLength <= kEpsilon)
			return MakeError(PathDiagnosticCode::ArcNormalParallelToChord, "Arc normal must not be parallel to its chord.");
		orthogonalNormal /= normalLength;
		const double radius = chordLength / (2.0 * std::sin(magnitude * 0.5));
		const double offset = chordLength / (2.0 * std::tan(static_cast<double>(signedSweep) * 0.5));
		const glm::dvec3 center = (a + b) * 0.5 + glm::cross(orthogonalNormal, chordDirection) * offset;
		const glm::dvec3 startDirection = (a - center) / radius;
		if (!std::isfinite(radius) || !IsFinite(center) || !IsFinite(startDirection))
			return MakeError(PathDiagnosticCode::ArcNonFiniteDerived, "Arc derivation produced a non-finite value.");
		return ArcGeometry{center, orthogonalNormal, startDirection, radius, std::atan2(startDirection.y, startDirection.x), signedSweep};
	}

	Result<PathSample> EvaluateSegment(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment, double t)
	{
		if (!std::isfinite(t) || t < 0.0 || t > 1.0)
			return MakeError(PathDiagnosticCode::NonFinite, "Segment parameter must be finite and in [0, 1].");
		if (Result<void> valid = CheckSegment(path, resolved, segment); !valid)
			return valid.Error();
		const glm::dvec3 a(resolved.positions[segment]);
		const glm::dvec3 b(resolved.positions[segment + 1]);
		const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[segment].data);
		if (cubic != nullptr && resolved.handlePositions.size() < (segment + 1) * 2)
			return MakeError(PathDiagnosticCode::NodeCountMismatch, "Resolved cubic control points are missing.");
		return std::visit([&](const auto &data) -> Result<PathSample> {
			using Data = std::decay_t<decltype(data)>;
			if constexpr (std::is_same_v<Data, LineSegmentData>)
			{
				const glm::dvec3 delta = b - a;
				const double length = glm::length(delta);
				if (length <= kEpsilon || !std::isfinite(length))
					return MakeError(PathDiagnosticCode::ZeroChord, "Line segment has no direction.");
				return PathSample{glm::mix(a, b, t), delta / length};
			}
			else if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
			{
				const glm::dvec3 c1(resolved.handlePositions[segment * 2]);
				const glm::dvec3 c2(resolved.handlePositions[segment * 2 + 1]);
				const double u = 1.0 - t;
				const glm::dvec3 position = u * u * u * a + 3.0 * u * u * t * c1 + 3.0 * u * t * t * c2 + t * t * t * b;
				const glm::dvec3 derivative = CubicDerivative(c1, c2, a, b, t);
				const double length = glm::length(derivative);
				if (length <= kEpsilon || !IsFinite(position) || !std::isfinite(length))
					return MakeError(PathDiagnosticCode::NonFinite, "Cubic segment has an invalid tangent.");
				return PathSample{position, derivative / length};
			}
			else
			{
				const Result<ArcGeometry> arc = DeriveArc(a, b, data.planeNormal, data.signedSweepRadians);
				if (!arc)
					return arc.Error();
				const double angle = arc->signedSweep * t;
				const glm::dvec3 radial = arc->startDirection * std::cos(angle) + glm::cross(arc->normal, arc->startDirection) * std::sin(angle);
				const glm::dvec3 tangent = (arc->signedSweep >= 0.0 ? 1.0 : -1.0) * glm::cross(arc->normal, radial);
				return PathSample{arc->center + arc->radius * radial, tangent};
			}
		}, path.segments[segment].data);
	}

	Result<double> SegmentLength(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment)
	{
		if (Result<void> valid = CheckSegment(path, resolved, segment); !valid)
			return valid.Error();
		const glm::dvec3 a(resolved.positions[segment]), b(resolved.positions[segment + 1]);
		return std::visit([&](const auto &data) -> Result<double> {
			using Data = std::decay_t<decltype(data)>;
			if constexpr (std::is_same_v<Data, LineSegmentData>)
				return glm::length(b - a);
			else if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
			{
				if (resolved.handlePositions.size() < (segment + 1) * 2)
					return MakeError(PathDiagnosticCode::NodeCountMismatch, "Resolved cubic control points are missing.");
				return CubicLength(
					glm::dvec3(resolved.handlePositions[segment * 2]),
					glm::dvec3(resolved.handlePositions[segment * 2 + 1]), a, b);
			}
			else
			{
				const Result<ArcGeometry> arc = DeriveArc(a, b, data.planeNormal, data.signedSweepRadians);
				return arc ? Result<double>(arc->radius * std::abs(arc->signedSweep)) : Result<double>(arc.Error());
			}
		}, path.segments[segment].data);
	}

	Result<double> SegmentParamAtLength(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment, double s)
	{
		if (!std::isfinite(s) || s < 0.0)
			return MakeError(PathDiagnosticCode::NonFinite, "Arc length parameter must be finite and non-negative.");
		const Result<double> length = SegmentLength(path, resolved, segment);
		if (!length)
			return length.Error();
		if (s > length.Value() + kEpsilon)
			return MakeError(PathDiagnosticCode::NonFinite, "Arc length parameter exceeds segment length.");
		if (length.Value() <= kEpsilon)
			return MakeError(PathDiagnosticCode::ZeroChord, "Cannot invert a zero-length segment.");
		const PathSegmentData &data = path.segments[segment].data;
		if (std::holds_alternative<LineSegmentData>(data) || std::holds_alternative<CircularArcSegmentData>(data))
			return std::clamp(s / length.Value(), 0.0, 1.0);
		if (resolved.handlePositions.size() < (segment + 1) * 2)
			return MakeError(PathDiagnosticCode::NodeCountMismatch, "Resolved cubic control points are missing.");
		const glm::dvec3 a(resolved.positions[segment]), b(resolved.positions[segment + 1]);
		const glm::dvec3 c1(resolved.handlePositions[segment * 2]);
		const glm::dvec3 c2(resolved.handlePositions[segment * 2 + 1]);
		double low = 0.0, high = 1.0;
		for (int iteration = 0; iteration < 80; ++iteration)
		{
			const double middle = (low + high) * 0.5;
			const Result<double> partial = CubicLength(c1, c2, a, b, middle);
			if (!partial)
				return partial.Error();
			if (partial.Value() < s)
				low = middle;
			else
				high = middle;
		}
		return (low + high) * 0.5;
	}

	std::vector<double> CumulativeLengths(const ScenePath &path, const ResolvedNodes &resolved)
	{
		if (!ValidatePath(path).empty() || resolved.positions.size() != path.nodes.size())
			return {};
		std::vector<double> lengths{0.0};
		lengths.reserve(path.segments.size() + 1);
		for (std::size_t segment = 0; segment < path.segments.size(); ++segment)
		{
			const Result<double> length = SegmentLength(path, resolved, segment);
			if (!length || !std::isfinite(length.Value()))
				return {};
			lengths.push_back(lengths.back() + length.Value());
		}
		return lengths;
	}
}
