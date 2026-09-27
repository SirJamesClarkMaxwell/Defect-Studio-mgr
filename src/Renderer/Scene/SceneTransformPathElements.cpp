#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformPathElements.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/epsilon.hpp>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathHandleRules.hpp"
#include "Renderer/Path/PathSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		PathEditContext Context(RendererWindowState &window)
		{
			// ViewportModalTransform owns the operation-wide snapshot and commits it once when the
			// modal ends. Per-frame path writes must therefore be command-validated but silent, otherwise
			// one drag would create one undo entry per frame.
			return {&window, {}};
		}

		const PathNode *FindNode(const ScenePath &path, const PathElementId id)
		{
			for (const PathNode &node : path.nodes)
				if (node.id == id)
					return &node;
			return nullptr;
		}

		PathNode *FindNode(ScenePath &path, const PathElementId id)
		{
			for (PathNode &node : path.nodes)
				if (node.id == id)
					return &node;
			return nullptr;
		}

		const PathHandle *FindHandle(const ScenePath &path, const PathElementId id, PathElementId &owner)
		{
			for (std::size_t index = 0; index < path.segments.size() && index + 1 < path.nodes.size(); ++index)
			{
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
				if (cubic == nullptr)
					continue;
				if (cubic->startHandle.id == id)
				{
					owner = path.nodes[index].id;
					return &cubic->startHandle;
				}
				if (cubic->endHandle.id == id)
				{
					owner = path.nodes[index + 1].id;
					return &cubic->endHandle;
				}
			}
			return nullptr;
		}

		PathHandle *FindHandle(ScenePath &path, const PathElementId id, PathElementId &owner)
		{
			for (std::size_t index = 0; index < path.segments.size() && index + 1 < path.nodes.size(); ++index)
			{
				auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
				if (cubic == nullptr)
					continue;
				if (cubic->startHandle.id == id)
				{
					owner = path.nodes[index].id;
					return &cubic->startHandle;
				}
				if (cubic->endHandle.id == id)
				{
					owner = path.nodes[index + 1].id;
					return &cubic->endHandle;
				}
			}
			return nullptr;
		}

		const PathSegment *FindSegment(const ScenePath &path, const PathElementId id, std::size_t &index)
		{
			for (std::size_t candidate = 0; candidate < path.segments.size(); ++candidate)
				if (path.segments[candidate].id == id)
				{
					index = candidate;
					return &path.segments[candidate];
				}
			return nullptr;
		}

		bool ContainsNode(const std::vector<PathElementId> &nodes, const PathElementId id)
		{
			return std::find(nodes.begin(), nodes.end(), id) != nodes.end();
		}

		void AppendNode(
			std::vector<PathElementTransformStart> &elements, std::vector<PathElementId> &nodes,
			const SceneObjectId path, const PathNode &node)
		{
			if (ContainsNode(nodes, node.id))
				return;
			nodes.push_back(node.id);
			elements.push_back({path, node.id, false, node.position});
		}

		glm::vec3 LocalToWorld(const ScenePath &path, const glm::vec3 &position)
		{
			return path.transform.position + path.transform.rotation * (path.transform.scale * position);
		}

		glm::vec3 WorldToLocal(const ScenePath &path, const glm::vec3 &position)
		{
			const glm::vec3 rotated = glm::inverse(path.transform.rotation) * (position - path.transform.position);
			return rotated / path.transform.scale;
		}

		[[nodiscard]] std::optional<glm::vec3> FindResolvedHandleOwnerPosition(
			const ScenePath &path, const ResolvedNodes &resolved, const PathElementId id)
		{
			PathElementId owner;
			if (FindHandle(path, id, owner) == nullptr)
				return std::nullopt;
			for (std::size_t index = 0; index < path.nodes.size(); ++index)
				if (path.nodes[index].id == owner && index < resolved.positions.size())
					return resolved.positions[index];
			return std::nullopt;
		}

		[[nodiscard]] std::optional<glm::vec3> FindResolvedHandlePosition(
			const ScenePath &path, const ResolvedNodes &resolved, const PathElementId id)
		{
			for (std::size_t index = 0; index < path.segments.size(); ++index)
			{
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
				if (cubic == nullptr)
					continue;
				if (cubic->startHandle.id == id && index * 2 < resolved.handlePositions.size())
					return resolved.handlePositions[index * 2];
				if (cubic->endHandle.id == id && index * 2 + 1 < resolved.handlePositions.size())
					return resolved.handlePositions[index * 2 + 1];
			}
			return std::nullopt;
		}

		// A zero on any scale axis collapses that axis, so the world-to-local divide above has no
		// finite answer and would write inf or NaN straight into an authored node position - which
		// then persists to the project file. Object Mode can reach a zero scale with S 0 Enter, so
		// this is not a theoretical input.
		//
		// The whole apply is refused rather than clamped: under a collapsed axis every world point
		// on it maps to the same local value, so there is no move to express, and a clamp would
		// invent one.
		[[nodiscard]] bool HasInvertibleTransform(const ScenePath &path)
		{
			constexpr float kMinScale = 1e-6f;
			const glm::vec3 &scale = path.transform.scale;
			return glm::all(glm::greaterThan(glm::abs(scale), glm::vec3(kMinScale))) &&
				std::isfinite(scale.x) && std::isfinite(scale.y) && std::isfinite(scale.z);
		}

		glm::vec3 Pivot(
			const TransformPivotMode mode, const glm::vec3 &worldPosition, const glm::vec3 &selectionPivot)
		{
			return mode == TransformPivotMode::IndividualOrigins ? worldPosition : selectionPivot;
		}

		void TransformNode(
			ScenePath &path, PathNode &node, const PathElementTransformStart &start,
			const SceneTransformDelta &delta, const TransformPivotMode pivotMode,
			const glm::vec3 &selectionPivot)
		{
			const glm::vec3 worldStart = LocalToWorld(path, start.position);
			const glm::vec3 worldTarget = ApplyTransformDelta(
				delta.spatial, worldStart, Pivot(pivotMode, worldStart, selectionPivot));
			node.position = WorldToLocal(path, worldTarget);
		}

		void TransformHandle(
			ScenePath &path, PathHandle &handle, const PathElementId owner,
			const PathElementTransformStart &start, const SceneTransformDelta &delta)
		{
			const PathNode *node = FindNode(path, owner);
			if (node == nullptr)
				return;
			const glm::vec3 worldStart = LocalToWorld(path, node->position + start.position);
			const glm::vec3 worldTarget = ApplyTransformDelta(
				delta.spatial, worldStart, LocalToWorld(path, node->position));
			handle.offset = WorldToLocal(path, worldTarget) - node->position;
		}

		void RestoreElement(ScenePath &path, const PathElementTransformStart &start)
		{
			if (!start.isHandle)
			{
				if (PathNode *node = FindNode(path, start.element); node != nullptr)
					node->position = start.position;
				return;
			}

			PathElementId owner;
			if (PathHandle *handle = FindHandle(path, start.element, owner); handle != nullptr)
				handle->offset = start.position;
		}

		void TransformPath(
			ScenePath &path, const std::vector<const PathElementTransformStart *> &elements,
			const SceneTransformDelta &delta, const TransformPivotMode pivotMode,
			const glm::vec3 &selectionPivot)
		{
			if (!HasInvertibleTransform(path))
				return;

			std::vector<PathElementId> movingNodes;
			for (const PathElementTransformStart *start : elements)
				if (!start->isHandle)
					movingNodes.push_back(start->element);

			for (const PathElementTransformStart *start : elements)
			{
				if (!start->isHandle)
				{
					if (PathNode *node = FindNode(path, start->element); node != nullptr)
						TransformNode(path, *node, *start, delta, pivotMode, selectionPivot);
					continue;
				}

				PathElementId owner;
				PathHandle *handle = FindHandle(path, start->element, owner);
				if (handle != nullptr && !ContainsNode(movingNodes, owner))
				{
					// Handle rotation and scale always use the owner node as pivot; pivot mode does not apply.
					TransformHandle(path, *handle, owner, *start, delta);
				}
			}
		}

		enum class PathElementPosition
		{
			Pivot,
			Anchor,
		};
	} // namespace

	std::vector<glm::vec3> CollectSceneTransformPathPositions(
		const SceneTransformSelectionSnapshot &snapshot, const RendererWindowState *window,
		const BindingContext *bindingContext, const PathElementPosition positionKind)
	{
		std::vector<glm::vec3> positions;
		positions.reserve(snapshot.paths.size() + snapshot.pathElements.size());
		for (const PathTransformStart &start : snapshot.paths)
		{
			const ScenePath *path = window != nullptr && window->paths != nullptr
				? window->paths->Store().Find(start.id) : nullptr;
			if (path != nullptr && bindingContext != nullptr)
			{
				const ResolvedNodes resolved = ResolveNodePositions(*path, *bindingContext);
				positions.insert(positions.end(), resolved.positions.begin(), resolved.positions.end());
			}
			else
				positions.push_back(start.transform.position);
		}
		for (const PathElementTransformStart &element : snapshot.pathElements)
		{
			if (window == nullptr || window->paths == nullptr || bindingContext == nullptr)
			{
				positions.push_back(element.position);
				continue;
			}
			const ScenePath *path = window->paths->Store().Find(element.path);
			if (path == nullptr)
				continue;
			// Edit Mode draws and picks markers from the renderer's empty binding context. The pivot
			// must use that same resolved geometry or a bound node/handle can show in one place while
			// the modal constraint line is anchored at the live binding target elsewhere.
			const ResolvedNodes resolved = ResolveNodePositions(*path, BindingContext{});
			if (!element.isHandle)
			{
				for (std::size_t index = 0; index < path->nodes.size(); ++index)
					if (path->nodes[index].id == element.element && index < resolved.positions.size())
						positions.push_back(resolved.positions[index]);
				continue;
			}
			const std::optional<glm::vec3> position = positionKind == PathElementPosition::Anchor
				? FindResolvedHandlePosition(*path, resolved, element.element)
				: FindResolvedHandleOwnerPosition(*path, resolved, element.element);
			if (position.has_value())
				positions.push_back(*position);
		}
		return positions;
	}

	std::vector<glm::vec3> CollectSceneTransformPathPivotPositions(
		const SceneTransformSelectionSnapshot &snapshot, const RendererWindowState *window,
		const BindingContext *bindingContext)
	{
		return CollectSceneTransformPathPositions(
			snapshot, window, bindingContext, PathElementPosition::Pivot);
	}

	std::vector<glm::vec3> CollectSceneTransformPathAnchorPositions(
		const SceneTransformSelectionSnapshot &snapshot, const RendererWindowState *window,
		const BindingContext *bindingContext)
	{
		return CollectSceneTransformPathPositions(
			snapshot, window, bindingContext, PathElementPosition::Anchor);
	}

	void CaptureSceneTransformPathElements(
		const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot)
	{
		snapshot.paths.clear();
		snapshot.pathElements.clear();
		if (window.paths == nullptr || !window.pathEdit.IsActive() || window.pathEdit.Selection().empty())
			return;

		const ScenePath *path = window.paths->Store().Find(window.pathEdit.Path());
		if (path == nullptr)
			return;

		std::vector<PathElementId> movingNodes;
		for (const PathElementId selected : window.pathEdit.Selection())
		{
			if (const PathNode *node = FindNode(*path, selected); node != nullptr)
			{
				AppendNode(snapshot.pathElements, movingNodes, path->id, *node);
				continue;
			}

			std::size_t segmentIndex = 0;
			if (FindSegment(*path, selected, segmentIndex) != nullptr && segmentIndex + 1 < path->nodes.size())
			{
				AppendNode(snapshot.pathElements, movingNodes, path->id, path->nodes[segmentIndex]);
				AppendNode(snapshot.pathElements, movingNodes, path->id, path->nodes[segmentIndex + 1]);
			}
		}

		for (const PathElementId selected : window.pathEdit.Selection())
		{
			PathElementId owner;
			const PathHandle *handle = FindHandle(*path, selected, owner);
			if (handle != nullptr && !ContainsNode(movingNodes, owner))
				snapshot.pathElements.push_back({path->id, selected, true, handle->offset});
		}
	}

	void ApplySceneTransformPathElements(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, const ModalTransformOp,
		const TransformPivotMode pivotMode, const glm::vec3 &selectionPivot)
	{
		if (window.paths == nullptr || snapshot.pathElements.empty())
			return;
		std::vector<SceneObjectId> ids;
		for (const PathElementTransformStart &start : snapshot.pathElements)
			if (std::find(ids.begin(), ids.end(), start.path) == ids.end())
				ids.push_back(start.path);
		ApplyPathEdit(Context(window), ids, PathRevisionKind::Geometry, "Transform path elements",
			[&](ScenePath &path) {
				std::vector<const PathElementTransformStart *> elements;
				for (const PathElementTransformStart &start : snapshot.pathElements)
					if (start.path == path.id)
						elements.push_back(&start);
				if (elements.empty())
					return Result<void>{};
				TransformPath(path, elements, delta, pivotMode, selectionPivot);
				const Result<void> handles = ApplyAutoHandles(path);
				return handles ? Result<void>{} : Result<void>(handles.Error());
			});
	}

	void RestoreSceneTransformPathElements(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot)
	{
		if (window.paths == nullptr || snapshot.pathElements.empty())
			return;
		std::vector<SceneObjectId> ids;
		for (const PathElementTransformStart &start : snapshot.pathElements)
			if (std::find(ids.begin(), ids.end(), start.path) == ids.end())
				ids.push_back(start.path);
		ApplyPathEdit(Context(window), ids, PathRevisionKind::Geometry, "Restore path elements",
			[&](ScenePath &path) {
				for (const PathElementTransformStart &start : snapshot.pathElements)
					if (start.path == path.id)
						RestoreElement(path, start);
				const Result<void> handles = ApplyAutoHandles(path);
				return handles ? Result<void>{} : Result<void>(handles.Error());
			});
	}
} // namespace DefectStudio
