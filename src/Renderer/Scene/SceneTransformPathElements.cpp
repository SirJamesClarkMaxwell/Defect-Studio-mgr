#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformPathElements.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include <glm/gtc/epsilon.hpp>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathHandleRules.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	glm::vec3 ScenePathWorldToLocal(const ScenePath &path, const glm::vec3 &position)
	{
		const glm::vec3 rotated = glm::inverse(path.transform.rotation) * (position - path.transform.position);
		return rotated / path.transform.scale;
	}

	// A collapsed scale has no finite inverse. Refuse local writes rather than persisting NaNs.
	bool HasInvertibleScenePathTransform(const ScenePath &path)
	{
		constexpr float kMinScale = 1e-6f;
		const glm::vec3 &scale = path.transform.scale;
		return glm::all(glm::greaterThan(glm::abs(scale), glm::vec3(kMinScale))) &&
			std::isfinite(scale.x) && std::isfinite(scale.y) && std::isfinite(scale.z);
	}

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
			PathElementTransformStart start{path, node.id, false, node.position};
			std::visit([&](const auto &binding) {
				if constexpr (!std::is_same_v<std::decay_t<decltype(binding)>, PathBinding::Free>)
				{
					start.bound = true;
					start.bindingOffset = binding.offset;
				}
			}, node.binding.value);
			elements.push_back(start);
		}

		glm::vec3 LocalToWorld(const ScenePath &path, const glm::vec3 &position)
		{
			return path.transform.position + path.transform.rotation * (path.transform.scale * position);
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

		glm::vec3 Pivot(
			const TransformPivotMode mode, const glm::vec3 &worldPosition, const glm::vec3 &selectionPivot)
		{
			return mode == TransformPivotMode::IndividualOrigins ? worldPosition : selectionPivot;
		}

		void TransformNode(
			ScenePath &path, PathNode &node, const PathElementTransformStart &start,
			const ResolvedNodes &resolved,
			const SceneTransformDelta &delta, const TransformPivotMode pivotMode,
			const glm::vec3 &selectionPivot)
		{
			const auto found = std::find_if(path.nodes.begin(), path.nodes.end(),
				[&](const PathNode &candidate) { return candidate.id == node.id; });
			const glm::vec3 worldStart = start.bound
				? resolved.positions[static_cast<std::size_t>(found - path.nodes.begin())]
				: LocalToWorld(path, start.position);
			const glm::vec3 worldTarget = ApplyTransformDelta(
				delta.spatial, worldStart, Pivot(pivotMode, worldStart, selectionPivot));
			if (start.bound)
				std::visit([&](auto &binding) {
					if constexpr (!std::is_same_v<std::decay_t<decltype(binding)>, PathBinding::Free>)
						binding.offset = start.bindingOffset + (worldTarget - worldStart);
				}, node.binding.value);
			else
				node.position = ScenePathWorldToLocal(path, worldTarget);
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
			handle.offset = ScenePathWorldToLocal(path, worldTarget) - node->position;
		}

		void RestoreElement(ScenePath &path, const PathElementTransformStart &start)
		{
			if (!start.isHandle)
			{
				if (PathNode *node = FindNode(path, start.element); node != nullptr)
				{
					if (start.bound)
						std::visit([&](auto &binding) {
							if constexpr (!std::is_same_v<std::decay_t<decltype(binding)>, PathBinding::Free>)
								binding.offset = start.bindingOffset;
						}, node->binding.value);
					else
						node->position = start.position;
				}
				return;
			}

			PathElementId owner;
			if (PathHandle *handle = FindHandle(path, start.element, owner); handle != nullptr)
				handle->offset = start.position;
		}

		void TransformPath(
			ScenePath &path, const std::vector<const PathElementTransformStart *> &elements,
			const BindingContext &bindingContext,
			const SceneTransformDelta &delta, const TransformPivotMode pivotMode,
			const glm::vec3 &selectionPivot)
		{
			if (!HasInvertibleScenePathTransform(path) && std::any_of(elements.begin(), elements.end(),
				[](const auto &start) { return !start->bound; }))
				return;
			// Resolve from the captured offsets, not last frame's preview, so R/S never accumulate.
			for (const auto &start : elements)
				RestoreElement(path, *start);
			const ResolvedNodes resolved = ResolveNodePositions(path, bindingContext);

			std::vector<PathElementId> movingNodes;
			for (const PathElementTransformStart *start : elements)
				if (!start->isHandle)
					movingNodes.push_back(start->element);

			for (const PathElementTransformStart *start : elements)
			{
				if (!start->isHandle)
				{
					if (PathNode *node = FindNode(path, start->element); node != nullptr)
						TransformNode(path, *node, *start, resolved, delta, pivotMode, selectionPivot);
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
			// Markers and the pivot use the same live binding context, so bound nodes and handles
			// agree with the modal constraint line's anchor.
			const ResolvedNodes resolved = ResolveNodePositions(*path, *bindingContext);
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
				TransformPath(path, elements, SceneSystem::MakePathBindingContext(window), delta, pivotMode, selectionPivot);
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
