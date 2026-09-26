#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformPaths.hpp"

#include <algorithm>

#include "Renderer/Path/PathCommands.hpp"
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

		glm::vec3 Pivot(const PathTransformStart &start, const TransformPivotMode mode, const glm::vec3 &selectionPivot)
		{
			if (mode != TransformPivotMode::IndividualOrigins || start.nodePositions.empty())
				return selectionPivot;
			return start.nodePositions.front();
		}

		void TransformPath(ScenePath &path, const PathTransformStart &start, const SceneTransformDelta &delta,
			ModalTransformOp operation, TransformPivotMode pivotMode, const glm::vec3 &selectionPivot)
		{
			const glm::vec3 pivot = Pivot(start, pivotMode, selectionPivot);
			for (std::size_t i = 0; i < start.nodes.size() && i < start.nodePositions.size(); ++i)
				for (PathNode &node : path.nodes)
					if (node.id == start.nodes[i])
						node.position = ApplyTransformDelta(delta.spatial, start.nodePositions[i], pivot);
			for (std::size_t i = 0; i < start.handles.size() && i < start.handlePositions.size(); ++i)
				for (PathSegment &segment : path.segments)
					if (auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
					{
						if (cubic->startHandle.id == start.handles[i])
							cubic->startHandle.position = ApplyTransformDelta(delta.spatial, start.handlePositions[i], pivot);
						if (cubic->endHandle.id == start.handles[i])
							cubic->endHandle.position = ApplyTransformDelta(delta.spatial, start.handlePositions[i], pivot);
					}
			// Unlike a label or an orbital, a path has no scalar rotation or scale of its own to
			// carry - V1 paths are points, so the spatial delta is the whole transform and
			// delta.rotationRadians / delta.scaleFactor have nothing to apply to. `operation` is
			// taken for signature parity with the other kinds and is deliberately unread.
			(void)operation;
		}

		void RestorePath(ScenePath &path, const PathTransformStart &start)
		{
			for (std::size_t i = 0; i < start.nodes.size() && i < start.nodePositions.size(); ++i)
				for (PathNode &node : path.nodes)
					if (node.id == start.nodes[i])
						node.position = start.nodePositions[i];
			for (std::size_t i = 0; i < start.handles.size() && i < start.handlePositions.size(); ++i)
				for (PathSegment &segment : path.segments)
					if (auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
					{
						if (cubic->startHandle.id == start.handles[i])
							cubic->startHandle.position = start.handlePositions[i];
						if (cubic->endHandle.id == start.handles[i])
							cubic->endHandle.position = start.handlePositions[i];
					}
		}
	}

	void CaptureSceneTransformPaths(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot)
	{
		if (window.paths == nullptr)
			return;
		for (const SceneObjectId id : window.selectedScenePaths)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			if (path == nullptr)
				continue;
			PathTransformStart start;
			start.id = id;
			for (const PathNode &node : path->nodes)
			{
				start.nodes.push_back(node.id);
				start.nodePositions.push_back(node.position);
			}
			for (const PathSegment &segment : path->segments)
				if (const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
				{
					start.handles.push_back(cubic->startHandle.id);
					start.handlePositions.push_back(cubic->startHandle.position);
					start.handles.push_back(cubic->endHandle.id);
					start.handlePositions.push_back(cubic->endHandle.position);
				}
			snapshot.paths.push_back(std::move(start));
		}
	}

	void ApplySceneTransformPaths(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, const ModalTransformOp operation,
		const TransformPivotMode pivotMode, const glm::vec3 &selectionPivot)
	{
		if (window.paths == nullptr || snapshot.paths.empty())
			return;
		std::vector<SceneObjectId> ids;
		for (const PathTransformStart &start : snapshot.paths)
			ids.push_back(start.id);
		ApplyPathEdit(Context(window), ids, PathRevisionKind::Geometry, "Transform paths",
			[&](ScenePath &path) {
				const auto found = std::find_if(snapshot.paths.begin(), snapshot.paths.end(),
					[&path](const PathTransformStart &start) { return start.id == path.id; });
				if (found == snapshot.paths.end())
					return Result<void>{};
				TransformPath(path, *found, delta, operation, pivotMode, selectionPivot);
				return Result<void>{};
			});
	}

	void RestoreSceneTransformPaths(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot)
	{
		if (window.paths == nullptr || snapshot.paths.empty())
			return;
		std::vector<SceneObjectId> ids;
		for (const PathTransformStart &start : snapshot.paths)
			ids.push_back(start.id);
		ApplyPathEdit(Context(window), ids, PathRevisionKind::Geometry, "Restore paths",
			[&](ScenePath &path) {
				const auto found = std::find_if(snapshot.paths.begin(), snapshot.paths.end(),
					[&path](const PathTransformStart &start) { return start.id == path.id; });
				if (found != snapshot.paths.end())
					RestorePath(path, *found);
				return Result<void>{};
			});
	}
}
