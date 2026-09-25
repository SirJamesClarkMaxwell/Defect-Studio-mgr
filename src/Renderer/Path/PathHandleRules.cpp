#include "Core/dspch.hpp"

#include "Renderer/Path/PathHandleRules.hpp"

#include <algorithm>
#include <cmath>

#include "Renderer/Path/PathEvaluator.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr double kEpsilon = 1.0e-12;

		[[nodiscard]] StructuredError MakeError(PathDiagnosticCode code, const char *detail)
		{
			return {ErrorCategory::Validation, Severity::Error, "Invalid tangent edit.", detail,
				"Correct the path geometry and try again.", "Renderer/Path", PathDiagnosticCodeName(code), DisplayPolicy::Silent};
		}

		[[nodiscard]] Result<void> CheckTopology(const ScenePath &path)
		{
			if (path.segments.size() + 1 != path.nodes.size())
				return MakeError(PathDiagnosticCode::NodeCountMismatch, "Path nodes and segments do not form an open path.");
			return {};
		}

		[[nodiscard]] ResolvedNodes ResolveAuthored(const ScenePath &path)
		{
			ResolvedNodes resolved;
			resolved.positions.reserve(path.nodes.size());
			for (const PathNode &node : path.nodes)
				resolved.positions.push_back(node.position);
			return resolved;
		}

		[[nodiscard]] Result<glm::dvec3> Direction(glm::vec3 from, glm::vec3 to)
		{
			const glm::dvec3 chord = glm::dvec3(to) - glm::dvec3(from);
			const double length = glm::length(chord);
			if (!std::isfinite(length))
				return MakeError(PathDiagnosticCode::NonFinite, "A tangent chord is not finite.");
			if (length <= kEpsilon)
				return MakeError(PathDiagnosticCode::ZeroChord, "A tangent requires a non-zero chord.");
			return chord / length;
		}

		void SetPosition(PathHandle &handle, glm::dvec3 position, bool autoOnly)
		{
			if (!autoOnly || handle.type == BezierHandleType::Auto)
				handle.position = glm::vec3(position);
		}

		[[nodiscard]] bool HasAutoAt(const ScenePath &path, std::size_t node)
		{
			if (node > 0 && std::holds_alternative<CubicBezierSegmentData>(path.segments[node - 1].data) &&
				std::get<CubicBezierSegmentData>(path.segments[node - 1].data).endHandle.type == BezierHandleType::Auto)
				return true;
			return node < path.segments.size() && std::holds_alternative<CubicBezierSegmentData>(path.segments[node].data) &&
				std::get<CubicBezierSegmentData>(path.segments[node].data).startHandle.type == BezierHandleType::Auto;
		}

		[[nodiscard]] Result<void> ApplyAtNode(ScenePath &path, std::size_t node, bool autoOnly)
		{
			const bool hasIncoming = node > 0;
			const bool hasOutgoing = node < path.segments.size();
			const bool incomingCubic = hasIncoming && std::holds_alternative<CubicBezierSegmentData>(path.segments[node - 1].data);
			const bool outgoingCubic = hasOutgoing && std::holds_alternative<CubicBezierSegmentData>(path.segments[node].data);
			if (!incomingCubic && !outgoingCubic)
				return MakeError(PathDiagnosticCode::TangentNotApplicable, "Neither adjacent segment has a cubic handle.");

			const glm::dvec3 pivot(path.nodes[node].position);
			if (!hasIncoming)
			{
				const Result<glm::dvec3> direction = Direction(path.nodes[node].position, path.nodes[node + 1].position);
				if (!direction)
					return direction.Error();
				auto &handle = std::get<CubicBezierSegmentData>(path.segments[node].data).startHandle;
				SetPosition(handle, pivot + direction.Value() * (glm::distance(pivot, glm::dvec3(path.nodes[node + 1].position)) / 3.0), autoOnly);
				return {};
			}
			if (!hasOutgoing)
			{
				const Result<glm::dvec3> direction = Direction(path.nodes[node - 1].position, path.nodes[node].position);
				if (!direction)
					return direction.Error();
				auto &handle = std::get<CubicBezierSegmentData>(path.segments[node - 1].data).endHandle;
				SetPosition(handle, pivot - direction.Value() * (glm::distance(pivot, glm::dvec3(path.nodes[node - 1].position)) / 3.0), autoOnly);
				return {};
			}

			if (incomingCubic && outgoingCubic)
			{
				const Result<glm::dvec3> incomingChord = Direction(path.nodes[node - 1].position, path.nodes[node].position);
				if (!incomingChord)
					return incomingChord.Error();
				const Result<glm::dvec3> outgoingChord = Direction(path.nodes[node].position, path.nodes[node + 1].position);
				if (!outgoingChord)
					return outgoingChord.Error();
				const Result<glm::dvec3> direction = Direction(path.nodes[node - 1].position, path.nodes[node + 1].position);
				if (!direction)
					return direction.Error();
				auto &incoming = std::get<CubicBezierSegmentData>(path.segments[node - 1].data).endHandle;
				auto &outgoing = std::get<CubicBezierSegmentData>(path.segments[node].data).startHandle;
				SetPosition(incoming, pivot - direction.Value() * (glm::distance(pivot, glm::dvec3(path.nodes[node - 1].position)) / 3.0), autoOnly);
				SetPosition(outgoing, pivot + direction.Value() * (glm::distance(pivot, glm::dvec3(path.nodes[node + 1].position)) / 3.0), autoOnly);
				return {};
			}

			const std::size_t rigid = incomingCubic ? node : node - 1;
			const double endpoint = incomingCubic ? 0.0 : 1.0;
			const Result<PathSample> sample = EvaluateSegment(path, ResolveAuthored(path), rigid, endpoint);
			if (!sample)
				return sample.Error();
			const double tangentLength = glm::length(sample->tangent);
			if (!std::isfinite(tangentLength) || tangentLength <= kEpsilon)
				return MakeError(PathDiagnosticCode::NonFinite, "The rigid segment has no finite tangent.");
			if (incomingCubic)
			{
				const Result<glm::dvec3> chord = Direction(path.nodes[node - 1].position, path.nodes[node].position);
				if (!chord)
					return chord.Error();
				auto &handle = std::get<CubicBezierSegmentData>(path.segments[node - 1].data).endHandle;
				SetPosition(handle, pivot - sample->tangent * (glm::distance(pivot, glm::dvec3(path.nodes[node - 1].position)) / 3.0), autoOnly);
			}
			else
			{
				const Result<glm::dvec3> chord = Direction(path.nodes[node].position, path.nodes[node + 1].position);
				if (!chord)
					return chord.Error();
				auto &handle = std::get<CubicBezierSegmentData>(path.segments[node].data).startHandle;
				SetPosition(handle, pivot + sample->tangent * (glm::distance(pivot, glm::dvec3(path.nodes[node + 1].position)) / 3.0), autoOnly);
			}
			return {};
		}
	} // namespace

	Result<void> MakeTangent(ScenePath &path, PathElementId node)
	{
		if (Result<void> valid = CheckTopology(path); !valid)
			return valid.Error();
		const auto found = std::find_if(path.nodes.begin(), path.nodes.end(), [node](const PathNode &candidate) { return candidate.id == node; });
		if (found == path.nodes.end())
			return MakeError(PathDiagnosticCode::UnknownElement, "The node does not exist in this path.");
		ScenePath edited = path;
		const std::size_t index = static_cast<std::size_t>(found - path.nodes.begin());
		if (Result<void> applied = ApplyAtNode(edited, index, false); !applied)
			return applied.Error();
		path = std::move(edited);
		return {};
	}

	Result<void> ApplyAutoHandles(ScenePath &path)
	{
		if (Result<void> valid = CheckTopology(path); !valid)
			return valid.Error();
		ScenePath edited = path;
		for (std::size_t node = 0; node < edited.nodes.size(); ++node)
		{
			if (!HasAutoAt(edited, node))
				continue;
			if (Result<void> applied = ApplyAtNode(edited, node, true); !applied)
				return applied.Error();
		}
		path = std::move(edited);
		return {};
	}
}
