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
			if (mode != TransformPivotMode::IndividualOrigins)
				return selectionPivot;
			return start.transform.position;
		}

		void TransformPath(ScenePath &path, const PathTransformStart &start, const SceneTransformDelta &delta,
			ModalTransformOp operation, TransformPivotMode pivotMode, const glm::vec3 &selectionPivot)
		{
			const glm::vec3 pivot = Pivot(start, pivotMode, selectionPivot);
			path.transform = start.transform;
			path.transform.position = ApplyTransformDelta(delta.spatial, start.transform.position, pivot);
			if (operation == ModalTransformOp::Rotate)
				path.transform.rotation = glm::normalize(delta.spatial.rotation * start.transform.rotation);
			else if (operation == ModalTransformOp::Scale)
			{
				const glm::mat3 basis(start.transform.rotation);
				const glm::mat3 localScale = glm::transpose(basis) * delta.spatial.linear * basis;
				path.transform.scale = start.transform.scale * glm::vec3(
					localScale[0][0], localScale[1][1], localScale[2][2]);
			}
		}

		void RestorePath(ScenePath &path, const PathTransformStart &start)
		{
			path.transform = start.transform;
		}
	}

	void CaptureSceneTransformPaths(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot)
	{
		if (window.paths == nullptr)
			return;
		std::vector<SceneObjectId> ids = window.selectedScenePaths;
		if (window.defectFrameSelected && window.showDefectFrame && window.structure.defectFrame && !window.pathEdit.IsActive())
			for (const SceneObjectId id : window.defectFrameChildren.paths)
				if (std::find(ids.begin(), ids.end(), id) == ids.end())
					ids.push_back(id);
		for (const SceneObjectId id : ids)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			if (path == nullptr)
				continue;
			PathTransformStart start;
			start.id = id;
			start.transform = path->transform;
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
