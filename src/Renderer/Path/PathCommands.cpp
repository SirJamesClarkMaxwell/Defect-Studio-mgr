#include "Core/dspch.hpp"

#include "Renderer/Path/PathCommands.hpp"

#include <cmath>
#include <type_traits>
#include <utility>

#include "Core/Utils/Memory.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeEditError(
			const char *code,
			const std::string &message,
			const std::string &details = {})
		{
			return {ErrorCategory::Validation, Severity::Error, message, details.empty() ? message : details,
				"Correct the path edit and try again.", "Renderer/Path/PathCommands", code, DisplayPolicy::Silent};
		}

		[[nodiscard]] StructuredError UnknownTargetError()
		{
			return MakeEditError("path.edit_unknown_target", "The path target does not exist.");
		}

		[[nodiscard]] StructuredError UnknownElementError()
		{
			return MakeEditError("path.edit_unknown_element", "The path element does not exist.");
		}

		[[nodiscard]] bool IsFinite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] PathNode *FindNode(ScenePath &path, const PathElementId id)
		{
			for (PathNode &node : path.nodes)
				if (node.id == id)
					return &node;
			return nullptr;
		}

		[[nodiscard]] PathSegment *FindSegment(ScenePath &path, const PathElementId id)
		{
			for (PathSegment &segment : path.segments)
				if (segment.id == id)
					return &segment;
			return nullptr;
		}

		[[nodiscard]] PathHandle *FindHandle(ScenePath &path, const PathElementId id)
		{
			for (PathSegment &segment : path.segments)
			{
				if (auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
				{
					if (cubic->startHandle.id == id)
						return &cubic->startHandle;
					if (cubic->endHandle.id == id)
						return &cubic->endHandle;
				}
			}
			return nullptr;
		}

		[[nodiscard]] StructuredError InvalidResultError(const std::vector<PathDiagnostic> &diagnostics)
		{
			const std::string details = diagnostics.empty() ? "The edit produced an invalid path." : diagnostics.front().message;
			return MakeEditError("path.edit_invalid_result", "The edit produced an invalid path.", details);
		}

		[[nodiscard]] StructuredError InactiveTransactionError()
		{
			return MakeEditError("path.edit_inactive_transaction", "The path drag transaction is not active.");
		}

	} // namespace

	PathEditReport ApplyPathEdit(
		const PathEditContext &context,
		std::span<const SceneObjectId> targets,
		const PathRevisionKind revision,
		const std::string &description,
		const std::function<Result<void>(ScenePath &)> &apply)
	{
		PathEditReport report;
		if (context.window == nullptr)
		{
			for (const SceneObjectId id : targets)
				report.skipped.push_back({id, UnknownTargetError()});
			return report;
		}

		if (context.window->paths == nullptr)
		{
			for (const SceneObjectId id : targets)
				report.skipped.push_back({id, UnknownTargetError()});
			return report;
		}
		PathSystem &system = *context.window->paths;
		const SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(*context.window);
		std::vector<std::pair<SceneObjectId, ScenePath>> accepted;
		accepted.reserve(targets.size());

		for (const SceneObjectId id : targets)
		{
			const ScenePath *stored = system.Store().Find(id);
			if (stored == nullptr)
			{
				report.skipped.push_back({id, UnknownTargetError()});
				continue;
			}

			ScenePath edited = *stored;
			const Result<void> result = apply(edited);
			if (!result)
			{
				report.skipped.push_back({id, result.Error()});
				continue;
			}
			const std::vector<PathDiagnostic> diagnostics = ValidatePath(edited);
			if (!diagnostics.empty())
			{
				report.skipped.push_back({id, InvalidResultError(diagnostics)});
				continue;
			}
			accepted.emplace_back(id, std::move(edited));
		}

		if (accepted.empty())
			return report;

		if (context.pushUndo)
			context.pushUndo(*context.window, before, description);
		for (auto &[id, edited] : accepted)
		{
			const auto commit = [&edited](ScenePath &path) { path = std::move(edited); };
			if (revision == PathRevisionKind::Geometry)
				system.Store().MutateGeometry(id, commit);
			else
				system.Store().MutateStyle(id, commit);
			report.applied.push_back(id);
		}
		return report;
	}

	Result<SceneObjectId> AddScenePath(const PathEditContext &context, ScenePath path)
	{
		if (context.window == nullptr)
			return MakeEditError("path.edit_unknown_target", "The scene window is not available.");
		if (!ValidatePath(path).empty())
			return MakeEditError("path.edit_invalid_result", "The path to add is invalid.");

		const SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(*context.window);
		const SceneObjectId id = SceneSystem::AppendScenePath(*context.window, std::move(path));
		if (!id.IsValid())
			return MakeEditError("path.edit_insert_failed", "The path could not be inserted into the scene.");
		if (context.pushUndo)
			context.pushUndo(*context.window, before, "Add path");
		SceneSystem::SyncLabelEntities(context.window->sceneRegistry, *context.window);
		return id;
	}

	PathEditReport DeleteScenePaths(const PathEditContext &context, std::span<const SceneObjectId> targets)
	{
		PathEditReport report;
		if (context.window == nullptr)
		{
			for (const SceneObjectId id : targets)
				report.skipped.push_back({id, UnknownTargetError()});
			return report;
		}

		if (context.window->paths == nullptr)
		{
			for (const SceneObjectId id : targets)
				report.skipped.push_back({id, UnknownTargetError()});
			return report;
		}
		PathSystem &system = *context.window->paths;
		std::vector<SceneObjectId> live;
		live.reserve(targets.size());
		for (const SceneObjectId id : targets)
		{
			if (system.Store().Contains(id))
				live.push_back(id);
			else
				report.skipped.push_back({id, UnknownTargetError()});
		}
		if (live.empty())
			return report;

		const SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(*context.window);
		if (context.pushUndo)
			context.pushUndo(*context.window, before, "Delete paths");
		for (const SceneObjectId id : live)
		{
			system.ErasePath(id);
			report.applied.push_back(id);
		}
		SceneSystem::SyncLabelEntities(context.window->sceneRegistry, *context.window);
		return report;
	}

	PathEditReport ReverseScenePaths(const PathEditContext &context, std::span<const SceneObjectId> targets)
	{
		return ApplyPathEdit(context, targets, PathRevisionKind::Geometry, "Reverse paths", [](ScenePath &path) {
			return ReversePath(path);
		});
	}

	Result<PathElementId> InsertScenePathNode(const PathEditContext &context, const SceneObjectId path, const std::size_t segment, const double t)
	{
		PathElementId inserted;
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Insert path node", [&inserted, segment, t](ScenePath &edited) {
			const Result<PathElementId> result = InsertNode(edited, segment, t);
			if (result)
				inserted = result.Value();
			return result ? Result<void>{} : Result<void>{result.Error()};
		});
		return report.applied.empty() ? Result<PathElementId>(report.skipped.front().reason) : Result<PathElementId>(inserted);
	}

	Result<PathElementId> ExtendScenePathEnd(
		const PathEditContext &context,
		const SceneObjectId path,
		const PathEnd end,
		const glm::vec3 newPosition)
	{
		PathElementId inserted;
		const PathEditReport report = ApplyPathEdit(
			context,
			std::span<const SceneObjectId>(&path, 1),
			PathRevisionKind::Geometry,
			"Extend path",
			[&inserted, end, newPosition](ScenePath &edited) {
				const Result<PathElementId> result = ExtendEnd(edited, end, newPosition);
				if (result)
					inserted = result.Value();
				return result ? Result<void>{} : Result<void>{result.Error()};
			});
		return report.applied.empty()
			? Result<PathElementId>(report.skipped.front().reason)
			: Result<PathElementId>(inserted);
	}

	Result<void> DeleteScenePathNode(const PathEditContext &context, const SceneObjectId path, const PathElementId node)
	{
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Delete path node", [node](ScenePath &edited) {
			return DeleteNode(edited, node);
		});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> DeleteScenePathNodes(
		const PathEditContext &context,
		const SceneObjectId path,
		const std::span<const PathElementId> nodes)
	{
		const PathEditReport report = ApplyPathEdit(
			context,
			std::span<const SceneObjectId>(&path, 1),
			PathRevisionKind::Geometry,
			"Delete path nodes",
			[nodes](ScenePath &edited) {
				for (const PathElementId node : nodes)
					if (const Result<void> result = DeleteNode(edited, node); !result)
						return result;
				return Result<void>{};
			});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> MoveScenePathNode(const PathEditContext &context, const SceneObjectId path, const PathElementId node, const glm::vec3 position)
	{
		if (!IsFinite(position))
			return MakeEditError("path.edit_non_finite", "The node position must be finite.");
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Move path node", [node, position](ScenePath &edited) {
			PathNode *target = FindNode(edited, node);
			if (target == nullptr)
				return Result<void>(UnknownElementError());
			target->position = position;
			return Result<void>{};
		});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> MoveScenePathHandle(const PathEditContext &context, const SceneObjectId path, const PathElementId handle, const glm::vec3 offset)
	{
		if (!IsFinite(offset))
			return MakeEditError("path.edit_non_finite", "The handle offset must be finite.");
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Move path handle", [handle, offset](ScenePath &edited) {
			PathHandle *target = FindHandle(edited, handle);
			if (target == nullptr)
				return Result<void>(UnknownElementError());
			target->offset = offset;
			return Result<void>{};
		});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> SetScenePathHandleType(const PathEditContext &context, const SceneObjectId path, const PathElementId handle, const BezierHandleType type)
	{
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Set path handle type", [handle, type](ScenePath &edited) {
			PathHandle *target = FindHandle(edited, handle);
			if (target == nullptr)
				return Result<void>(UnknownElementError());
			target->type = type;
			return Result<void>{};
		});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> SetScenePathHandleTypes(
		const PathEditContext &context,
		const SceneObjectId path,
		const std::span<const PathElementId> handles,
		const BezierHandleType type)
	{
		const PathEditReport report = ApplyPathEdit(
			context,
			std::span<const SceneObjectId>(&path, 1),
			PathRevisionKind::Geometry,
			"Set path handle types",
			[handles, type](ScenePath &edited) {
				for (const PathElementId handle : handles)
				{
					PathHandle *target = FindHandle(edited, handle);
					if (target == nullptr)
						return Result<void>(UnknownElementError());
					target->type = type;
				}
				return Result<void>{};
			});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> SetScenePathArcParameters(const PathEditContext &context, const SceneObjectId path, const PathElementId segment, const glm::vec3 planeNormal, const float signedSweepRadians)
	{
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Set path arc parameters", [segment, planeNormal, signedSweepRadians](ScenePath &edited) {
			PathSegment *target = FindSegment(edited, segment);
			if (target == nullptr)
				return Result<void>(UnknownElementError());
			auto *arc = std::get_if<CircularArcSegmentData>(&target->data);
			if (arc == nullptr)
				return Result<void>(MakeEditError("path.edit_invalid_result", "The target segment is not an arc."));
			// The arc's endpoints are its neighbouring nodes; a path whose node count does not match
			// its segment count never reaches here through an edit, but it can be inserted directly
			// into the store, and indexing past the end would be a crash rather than a rejection.
			const std::size_t index = static_cast<std::size_t>(target - edited.segments.data());
			if (index + 1 >= edited.nodes.size())
				return Result<void>(UnknownElementError());
			const Result<ArcGeometry> derived = DeriveArc(glm::dvec3(edited.nodes[index].position), glm::dvec3(edited.nodes[index + 1].position), planeNormal, signedSweepRadians);
			if (!derived)
				return Result<void>(derived.Error());
			arc->planeNormal = planeNormal;
			arc->signedSweepRadians = signedSweepRadians;
			return Result<void>{};
		});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> SetScenePathArcGeometry(
		const PathEditContext &context,
		const SceneObjectId path,
		const PathElementId segment,
		const PathArcParameters &parameters)
	{
		const Result<PathArcEndpoints> solved = SolveArcEndpoints(parameters);
		if (!solved)
			return solved.Error();
		const glm::vec3 start(solved->start);
		const glm::vec3 end(solved->end);
		const glm::vec3 axis(glm::normalize(parameters.axis));
		const float sweep = static_cast<float>(parameters.signedSweepRadians);
		if (!IsFinite(start) || !IsFinite(end) || !IsFinite(axis) || !std::isfinite(sweep))
			return MakeEditError("path.edit_non_finite", "The numeric arc parameters exceed path storage precision.");

		const PathEditReport report = ApplyPathEdit(
			context,
			std::span<const SceneObjectId>(&path, 1),
			PathRevisionKind::Geometry,
			"Edit path arc geometry",
			[segment, start, end, axis, sweep](ScenePath &edited) {
				PathSegment *target = FindSegment(edited, segment);
				if (target == nullptr)
					return Result<void>(UnknownElementError());
				auto *arc = std::get_if<CircularArcSegmentData>(&target->data);
				if (arc == nullptr)
					return Result<void>(MakeEditError("path.edit_invalid_result", "The target segment is not an arc."));
				const std::size_t index = static_cast<std::size_t>(target - edited.segments.data());
				if (index + 1 >= edited.nodes.size())
					return Result<void>(UnknownElementError());
				edited.nodes[index].position = start;
				edited.nodes[index + 1].position = end;
				arc->planeNormal = axis;
				arc->signedSweepRadians = sweep;
				return Result<void>{};
			});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	PathEditReport SetScenePathStyle(const PathEditContext &context, std::span<const SceneObjectId> targets, const std::function<void(PathStrokeStyle &)> &mutate)
	{
		return ApplyPathEdit(context, targets, PathRevisionKind::Style, "Edit path style", [&mutate](ScenePath &path) {
			mutate(path.style);
			return Result<void>{};
		});
	}

	Result<void> SetScenePathBinding(const PathEditContext &context, const SceneObjectId path, const PathElementId node, PathBinding binding)
	{
		const PathEditReport report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1), PathRevisionKind::Geometry, "Set path binding", [node, binding = std::move(binding)](ScenePath &edited) mutable {
			PathNode *target = FindNode(edited, node);
			if (target == nullptr)
				return Result<void>(UnknownElementError());
			target->binding = std::move(binding);
			return Result<void>{};
		});
		return report.applied.empty() ? Result<void>(report.skipped.front().reason) : Result<void>{};
	}

	Result<void> DetachScenePathBinding(const PathEditContext &context, const SceneObjectId path, const PathElementId node)
	{
		return SetScenePathBinding(context, path, node, PathBinding{});
	}

	void PathDragTransaction::Begin(PathEditContext context, std::string description)
	{
		m_Context = std::move(context);
		m_Description = std::move(description);
		m_Before.reset();
		m_Changed = false;
		m_Active = m_Context.window != nullptr;
		if (m_Active)
			m_Before = CaptureSceneObjectsSnapshot(*m_Context.window);
	}

	bool PathDragTransaction::Active() const noexcept
	{
		return m_Active;
	}

	PathEditReport PathDragTransaction::Update(
		std::span<const SceneObjectId> targets,
		const PathRevisionKind revision,
		const std::function<Result<void>(ScenePath &)> &apply)
	{
		if (!m_Active)
		{
			PathEditReport report;
			for (const SceneObjectId id : targets)
				report.skipped.push_back({id, InactiveTransactionError()});
			return report;
		}
		PathEditContext liveContext = m_Context;
		liveContext.pushUndo = {};
		PathEditReport report = ApplyPathEdit(liveContext, targets, revision, m_Description, apply);
		m_Changed = m_Changed || report.AnyApplied();
		return report;
	}

	void PathDragTransaction::Commit()
	{
		if (!m_Active)
			return;
		if (m_Changed && m_Before.has_value() && m_Context.pushUndo)
			m_Context.pushUndo(*m_Context.window, std::move(*m_Before), m_Description);
		m_Before.reset();
		m_Context = {};
		m_Description.clear();
		m_Changed = false;
		m_Active = false;
	}

	void PathDragTransaction::Cancel()
	{
		if (!m_Active)
			return;
		if (m_Before.has_value())
			RestoreSceneObjectsSnapshot(*m_Context.window, std::move(*m_Before));
		m_Before.reset();
		m_Context = {};
		m_Description.clear();
		m_Changed = false;
		m_Active = false;
	}
} // namespace DefectStudio
