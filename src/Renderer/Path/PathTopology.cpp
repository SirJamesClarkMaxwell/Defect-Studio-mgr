#include "Core/dspch.hpp"

#include "Renderer/Path/PathTopology.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "Renderer/Path/PathEvaluator.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeError(PathDiagnosticCode code, const char *detail)
		{
			return {ErrorCategory::Validation, Severity::Error, "Invalid path edit.", detail,
				"Correct the path data and try again.", "Renderer/Path", PathDiagnosticCodeName(code), DisplayPolicy::Silent};
		}

		[[nodiscard]] bool IsFinite(glm::vec3 value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] ResolvedNodes ResolveAuthored(const ScenePath &path)
		{
			ResolvedNodes resolved;
			resolved.positions.reserve(path.nodes.size());
			for (const PathNode &node : path.nodes)
				resolved.positions.push_back(node.position);
			return resolved;
		}

		[[nodiscard]] Result<void> CheckTopology(const ScenePath &path)
		{
			if (path.segments.size() + 1 != path.nodes.size())
				return MakeError(PathDiagnosticCode::NodeCountMismatch, "Path nodes and segments do not form an open path.");
			return {};
		}

		[[nodiscard]] PathSegment MakeCubicSegment(ScenePath &path, glm::vec3 a, glm::vec3 b)
		{
			const glm::vec3 delta = b - a;
			return {AllocateElementId(path), CubicBezierSegmentData{
				{AllocateElementId(path), a + delta / 3.0f, BezierHandleType::Vector},
				{AllocateElementId(path), b - delta / 3.0f, BezierHandleType::Vector}}};
		}
	} // namespace

	Result<PathElementId> InsertNode(ScenePath &path, std::size_t segment, double t)
	{
		if (Result<void> valid = CheckTopology(path); !valid)
			return valid.Error();
		if (segment >= path.segments.size())
			return MakeError(PathDiagnosticCode::InvalidSegmentIndex, "The segment index does not exist.");
		if (!std::isfinite(t))
			return MakeError(PathDiagnosticCode::NonFinite, "The split parameter is not finite.");
		if (t <= 0.0 || t >= 1.0)
			return MakeError(PathDiagnosticCode::ParameterOutOfRange, "The split parameter must be strictly between zero and one.");

		ScenePath edited = path;
		const PathSegment original = edited.segments[segment];
		const glm::vec3 a = edited.nodes[segment].position;
		const glm::vec3 b = edited.nodes[segment + 1].position;
		glm::vec3 point{0.0f};
		PathSegment right;
		if (std::holds_alternative<CircularArcSegmentData>(original.data))
		{
			const Result<PathSample> sample = EvaluateSegment(edited, ResolveAuthored(edited), segment, t);
			if (!sample)
				return sample.Error();
			point = glm::vec3(sample->position);
		}

		std::visit([&](const auto &data) {
			using Data = std::decay_t<decltype(data)>;
			if constexpr (std::is_same_v<Data, LineSegmentData>)
			{
				point = glm::vec3(glm::mix(glm::dvec3(a), glm::dvec3(b), t));
				edited.segments[segment].data = LineSegmentData{};
				right = {AllocateElementId(edited), LineSegmentData{}};
			}
			else if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
			{
				const glm::vec3 p01 = glm::mix(a, data.startHandle.position, static_cast<float>(t));
				const glm::vec3 p12 = glm::mix(data.startHandle.position, data.endHandle.position, static_cast<float>(t));
				const glm::vec3 p23 = glm::mix(data.endHandle.position, b, static_cast<float>(t));
				const glm::vec3 p012 = glm::mix(p01, p12, static_cast<float>(t));
				const glm::vec3 p123 = glm::mix(p12, p23, static_cast<float>(t));
				point = glm::mix(p012, p123, static_cast<float>(t));
				edited.segments[segment].data = CubicBezierSegmentData{
					{data.startHandle.id, p01, data.startHandle.type}, {AllocateElementId(edited), p012, data.startHandle.type}};
				right = {AllocateElementId(edited), CubicBezierSegmentData{
					{AllocateElementId(edited), p123, data.endHandle.type}, {data.endHandle.id, p23, data.endHandle.type}}};
			}
			else
			{
				edited.segments[segment].data = CircularArcSegmentData{data.planeNormal, data.signedSweepRadians * static_cast<float>(t)};
				right = {AllocateElementId(edited), CircularArcSegmentData{data.planeNormal, data.signedSweepRadians * static_cast<float>(1.0 - t)}};
			}
		}, original.data);

		const PathElementId nodeId = AllocateElementId(edited);
		edited.nodes.insert(edited.nodes.begin() + static_cast<std::ptrdiff_t>(segment + 1), {nodeId, point, {}});
		edited.segments.insert(edited.segments.begin() + static_cast<std::ptrdiff_t>(segment + 1), std::move(right));
		const std::vector<PathDiagnostic> diagnostics = ValidatePath(edited);
		if (!diagnostics.empty())
			return MakeError(diagnostics.front().code, diagnostics.front().message.c_str());
		path = std::move(edited);
		return nodeId;
	}

	Result<PathElementId> ExtendEnd(ScenePath &path, PathEnd end, glm::vec3 newPosition)
	{
		if (!IsFinite(newPosition))
			return MakeError(PathDiagnosticCode::NonFinite, "The new endpoint is not finite.");
		if (path.nodes.empty())
		{
			if (!path.segments.empty())
				return MakeError(PathDiagnosticCode::NodeCountMismatch, "An empty path cannot contain segments.");
			ScenePath edited = path;
			const PathElementId nodeId = AllocateElementId(edited);
			edited.nodes.push_back({nodeId, newPosition, {}});
			path = std::move(edited);
			return nodeId;
		}
		if (Result<void> valid = CheckTopology(path); !valid)
			return valid.Error();

		ScenePath edited = path;
		const PathElementId nodeId = AllocateElementId(edited);
		const glm::vec3 existing = end == PathEnd::Start ? edited.nodes.front().position : edited.nodes.back().position;
		if (glm::length(glm::dvec3(newPosition) - glm::dvec3(existing)) <= 1.0e-12)
			return MakeError(PathDiagnosticCode::ZeroChord, "An extended segment needs distinct endpoints.");
		PathSegment extension;
		if (edited.segments.empty())
			extension = {AllocateElementId(edited), LineSegmentData{}};
		else
		{
			const PathSegment &terminal = end == PathEnd::Start ? edited.segments.front() : edited.segments.back();
			extension = std::visit([&](const auto &data) -> PathSegment {
				using Data = std::decay_t<decltype(data)>;
				if constexpr (std::is_same_v<Data, LineSegmentData>)
					return {AllocateElementId(edited), LineSegmentData{}};
				else if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
					return MakeCubicSegment(edited, end == PathEnd::Start ? newPosition : existing, end == PathEnd::Start ? existing : newPosition);
				else
					return {AllocateElementId(edited), CircularArcSegmentData{data.planeNormal, data.signedSweepRadians}};
			}, terminal.data);
		}
		if (end == PathEnd::Start)
		{
			edited.nodes.insert(edited.nodes.begin(), {nodeId, newPosition, {}});
			edited.segments.insert(edited.segments.begin(), std::move(extension));
		}
		else
		{
			edited.nodes.push_back({nodeId, newPosition, {}});
			edited.segments.push_back(std::move(extension));
		}
		const std::vector<PathDiagnostic> diagnostics = ValidatePath(edited);
		if (!diagnostics.empty())
			return MakeError(diagnostics.front().code, diagnostics.front().message.c_str());
		path = std::move(edited);
		return nodeId;
	}

	Result<void> DeleteNode(ScenePath &path, PathElementId node)
	{
		if (Result<void> valid = CheckTopology(path); !valid)
			return valid.Error();
		const auto found = std::find_if(path.nodes.begin(), path.nodes.end(), [node](const PathNode &candidate) { return candidate.id == node; });
		if (found == path.nodes.end())
			return MakeError(PathDiagnosticCode::UnknownElement, "The node does not exist in this path.");
		if (path.nodes.size() == 1)
			return MakeError(PathDiagnosticCode::LastNodeNotRemovable, "The last path node cannot be removed.");

		ScenePath edited = path;
		const std::size_t index = static_cast<std::size_t>(found - path.nodes.begin());
		if (index == 0)
			edited.segments.erase(edited.segments.begin());
		else if (index + 1 == edited.nodes.size())
			edited.segments.pop_back();
		else
		{
			if (!std::holds_alternative<LineSegmentData>(edited.segments[index - 1].data) || !std::holds_alternative<LineSegmentData>(edited.segments[index].data))
				return MakeError(PathDiagnosticCode::MergeRequiresLineNeighbours, "Only adjacent line segments can be merged.");
			edited.segments.erase(edited.segments.begin() + static_cast<std::ptrdiff_t>(index));
		}
		edited.nodes.erase(edited.nodes.begin() + static_cast<std::ptrdiff_t>(index));
		path = std::move(edited);
		return {};
	}

	Result<void> ReversePath(ScenePath &path)
	{
		if (Result<void> valid = CheckTopology(path); !valid)
			return valid.Error();
		ScenePath edited = path;
		std::reverse(edited.nodes.begin(), edited.nodes.end());
		std::reverse(edited.segments.begin(), edited.segments.end());
		for (PathSegment &segment : edited.segments)
		{
			std::visit([](auto &data) {
				using Data = std::decay_t<decltype(data)>;
				if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
					std::swap(data.startHandle, data.endHandle);
				else if constexpr (std::is_same_v<Data, CircularArcSegmentData>)
					data.signedSweepRadians = -data.signedSweepRadians;
			}, segment.data);
		}
		std::swap(edited.style.startDecoration, edited.style.endDecoration);
		for (PathGradientStop &stop : edited.style.gradient.stops)
			stop.position = 1.0f - stop.position;
		std::reverse(edited.style.gradient.stops.begin(), edited.style.gradient.stops.end());
		const double dashLength = static_cast<double>(edited.style.dash.dashLength);
		const double gapLength = static_cast<double>(edited.style.dash.gapLength);
		if (edited.style.dash.enabled && std::isfinite(dashLength) && std::isfinite(gapLength) && dashLength > 0.0 && gapLength > 0.0)
		{
			const std::vector<double> lengths = CumulativeLengths(edited, ResolveAuthored(edited));
			if (!lengths.empty())
				edited.style.dash.phase = static_cast<float>(dashLength - lengths.back() - static_cast<double>(edited.style.dash.phase));
		}
		path = std::move(edited);
		return {};
	}
}
