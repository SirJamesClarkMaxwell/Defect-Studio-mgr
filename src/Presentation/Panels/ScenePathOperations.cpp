#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathOperations.hpp"

#include <algorithm>
#include <utility>
#include <variant>

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		// Same nudge every other scene object's copy gets, so a duplicate lands where the eye is
		// already looking for it.
		constexpr glm::vec3 kScenePathCopyOffset(0.5f, 0.0f, 0.0f);

		[[nodiscard]] std::vector<ScenePath> CollectSelected(const RendererWindowState &windowState)
		{
			std::vector<ScenePath> copies;
			if (windowState.paths == nullptr)
				return copies;
			for (const SceneObjectId id : windowState.selectedScenePaths)
				if (const ScenePath *path = windowState.paths->Store().Find(id); path != nullptr)
					copies.push_back(*path);
			return copies;
		}

		// Inserts each copy and returns the ids that actually landed. One snapshot has already been
		// pushed by the caller, so this runs on a context with no undo sink.
		[[nodiscard]] std::vector<SceneObjectId> AppendCopies(
			RendererWindowState &windowState, std::vector<ScenePath> copies)
		{
			const PathEditContext silent = MakeSilentPathEditContext(windowState);
			std::vector<SceneObjectId> newIds;
			newIds.reserve(copies.size());
			for (ScenePath &copy : copies)
			{
				OffsetAndDetachScenePath(copy, kScenePathCopyOffset);
				// AppendScenePath mints the id, so the copy must not arrive carrying the original's.
				copy.id = SceneObjectId{};
				if (const Result<SceneObjectId> added = AddScenePath(silent, std::move(copy)); added.HasValue())
					newIds.push_back(added.Value());
			}
			return newIds;
		}
	} // namespace

	PathEditContext MakeWindowPathEditContext(RendererWindowState &windowState)
	{
		PathEditContext context;
		context.window = &windowState;
		context.pushUndo = [](RendererWindowState &window, SceneObjectsSnapshot before, const std::string &) {
			PushSceneObjectsUndoSnapshot(window, std::move(before));
		};
		return context;
	}

	PathEditContext MakeSilentPathEditContext(RendererWindowState &windowState)
	{
		PathEditContext context;
		context.window = &windowState;
		return context;
	}

	std::vector<ScenePath> &GetScenePathClipboard()
	{
		static std::vector<ScenePath> clipboard;
		return clipboard;
	}

	void OffsetAndDetachScenePath(ScenePath &path, const glm::vec3 &offset)
	{
		for (PathNode &node : path.nodes)
		{
			node.position += offset;
			node.binding = PathBinding{};
		}
		for (PathSegment &segment : path.segments)
		{
			if (auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
			{
				cubic->startHandle.position += offset;
				cubic->endHandle.position += offset;
			}
		}
		// EnsureScenePersistKeys mints a fresh one; a shared key would collapse the copy and the
		// original into a single saved object (see the S9 persistKey collision).
		path.persistKey.clear();
	}

	void CopyScenePathsToClipboard(const RendererWindowState &windowState)
	{
		GetScenePathClipboard() = CollectSelected(windowState);
	}

	void DuplicateSelectedScenePaths(RendererWindowState &windowState)
	{
		std::vector<ScenePath> copies = CollectSelected(windowState);
		if (copies.empty())
			return;
		// One snapshot for the whole batch, before anything is inserted: otherwise a five-path
		// duplicate needs five undos to take back.
		PushSceneObjectsUndoSnapshot(windowState, CaptureSceneObjectsSnapshot(windowState));
		std::vector<SceneObjectId> newIds = AppendCopies(windowState, std::move(copies));
		if (!newIds.empty())
			windowState.selectedScenePaths = std::move(newIds);
	}

	void PasteScenePathsFromClipboard(RendererWindowState &windowState)
	{
		std::vector<ScenePath> copies = GetScenePathClipboard();
		if (copies.empty())
			return;
		PushSceneObjectsUndoSnapshot(windowState, CaptureSceneObjectsSnapshot(windowState));
		std::vector<SceneObjectId> newIds = AppendCopies(windowState, std::move(copies));
		if (!newIds.empty())
			windowState.selectedScenePaths = std::move(newIds);
	}

	void EraseScenePaths(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids)
	{
		if (ids.empty())
			return;
		// DeleteScenePaths pushes the one undo entry itself, so this goes through the live context.
		const std::vector<SceneObjectId> targets = ids;
		const PathEditReport report = DeleteScenePaths(MakeWindowPathEditContext(windowState), targets);
		if (!report.AnyApplied())
			return;
		std::erase_if(windowState.selectedScenePaths, [&report](const SceneObjectId id) {
			return std::find(report.applied.begin(), report.applied.end(), id) != report.applied.end();
		});
	}
} // namespace DefectStudio
