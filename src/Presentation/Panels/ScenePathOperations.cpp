#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathOperations.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <variant>

#include "Renderer/RendererLayer.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"

namespace DefectStudio
{
	namespace
	{
		// Same nudge every other scene object's copy gets, so a duplicate lands where the eye is
		// already looking for it.
		constexpr glm::vec3 kScenePathCopyOffset(0.5f, 0.0f, 0.0f);

		[[nodiscard]] StructuredError PathEditSelectionError(const char *code, const char *message)
		{
			return {ErrorCategory::Validation, Severity::Error, message, message,
				"Select a compatible path element and try again.", "Presentation/ScenePathOperations",
				code, DisplayPolicy::Silent};
		}

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

	void SelectAddedScenePaths(RendererWindowState &windowState, std::vector<SceneObjectId> ids)
	{
		windowState.pathEdit.Leave();
		windowState.selectedScenePaths = std::move(ids);
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedFreeLabels.clear();
		windowState.selectedSceneOrbitals.clear();
		windowState.selectedScenePlanes.clear();
		windowState.selectedVacancies.clear();
		windowState.defectFrameSelected = false;
		SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	float &GetScenePathAtomBuffer()
	{
		static float buffer = 1.15f;
		return buffer;
	}

	float GetDefaultSceneSegmentLength(const RendererWindowState &windowState)
	{
		glm::vec3 minimum(std::numeric_limits<float>::max());
		glm::vec3 maximum(std::numeric_limits<float>::lowest());
		for (const RendererAtomData &atom : windowState.structure.atoms)
		{
			minimum = glm::min(minimum, atom.cartesianPosition);
			maximum = glm::max(maximum, atom.cartesianPosition);
		}
		const float diagonal = windowState.structure.atoms.empty() ? 0.0f : glm::length(maximum - minimum);
		return std::isfinite(diagonal) && diagonal > 0.0f ? std::clamp(diagonal * 0.20f, 0.75f, 4.0f) : 1.0f;
	}

	Result<SceneObjectId> AddFreeScenePathSegment(
		RendererWindowState &windowState, const glm::vec3 &worldPosition, const bool arrow)
	{
		const float length = GetDefaultSceneSegmentLength(windowState);
		ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, worldPosition + glm::vec3(length * 0.5f, 0, 0));
		path.name = arrow ? "Arrow" : "Line";
		path.nodes.front().position = glm::vec3(-length * 0.5f, 0, 0);
		path.nodes.back().position = glm::vec3(length * 0.5f, 0, 0);
		path.style.endDecoration.kind = arrow ? PathDecorationKind::Arrow : PathDecorationKind::None;
		const auto added = AddScenePath(MakeWindowPathEditContext(windowState), std::move(path));
		if (added)
			SelectAddedScenePaths(windowState, {added.Value()});
		return added;
	}

	Result<SceneObjectId> AddScenePathThroughSelectedAtoms(RendererWindowState &windowState, const bool arrow)
	{
		// Ends: selected atoms, then selected vacancies, bound so both ends follow their sources.
		struct End
		{
			glm::vec3 position;
			PathBinding binding;
		};
		std::vector<End> ends;
		const float buffer = GetScenePathAtomBuffer();
		for (const auto index : windowState.selectedAtomIndices)
			if (index < windowState.structure.atoms.size())
				ends.push_back({windowState.structure.atoms[index].cartesianPosition,
					PathBinding{PathBinding::CopyPosition{index, {}, buffer}}});
		for (const auto index : windowState.selectedVacancies)
			if (index < windowState.structure.vacancies.size())
				ends.push_back({windowState.structure.vacancies[index].cartesianPosition,
					PathBinding{PathBinding::CopyVacancy{index, {}, buffer}}});
		if (ends.size() != 2)
			return PathEditSelectionError(
				"path.two_atoms_required", "Select exactly two atoms or vacancies (Ctrl+click) to draw a segment.");
		ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0));
		path.name = arrow ? "Arrow" : "Line";
		path.transform.position = glm::vec3(0);
		path.nodes[0].position = ends[0].position;
		path.nodes[1].position = ends[1].position;
		path.nodes[0].binding = ends[0].binding;
		path.nodes[1].binding = ends[1].binding;
		path.style.endDecoration.kind = arrow ? PathDecorationKind::Arrow : PathDecorationKind::None;
		MovePathOriginToCentre(path);
		const auto added = AddScenePath(MakeWindowPathEditContext(windowState), std::move(path));
		if (added)
			SelectAddedScenePaths(windowState, {added.Value()});
		return added;
	}

	std::vector<ScenePath> &GetScenePathClipboard()
	{
		static std::vector<ScenePath> clipboard;
		return clipboard;
	}

	void OffsetAndDetachScenePath(ScenePath &path, const glm::vec3 &offset)
	{
		path.transform.position += offset;
		for (PathNode &node : path.nodes)
		{
			node.binding = PathBinding{};
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

	Result<PathElementId> ExtendSelectedScenePathEnd(RendererWindowState &windowState)
	{
		if (!windowState.pathEdit.IsActive() || windowState.paths == nullptr ||
			windowState.pathEdit.ElementMode() != PathElementMode::NodeHandle)
			return PathEditSelectionError(
				"path.edit_endpoint_required", "Select an endpoint node in Path Edit Mode.");

		const SceneObjectId pathId = windowState.pathEdit.Path();
		const ScenePath *path = windowState.paths->Store().Find(pathId);
		if (path == nullptr || path->nodes.size() < 2)
			return PathEditSelectionError(
				"path.edit_endpoint_required", "The edited path needs a terminal segment to extend.");

		const PathElementId selected = windowState.pathEdit.ActiveElement();
		PathEnd end;
		glm::vec3 newPosition;
		if (selected == path->nodes.front().id)
		{
			end = PathEnd::Start;
			newPosition = path->nodes.front().position +
				(path->nodes.front().position - path->nodes[1].position);
		}
		else if (selected == path->nodes.back().id)
		{
			end = PathEnd::End;
			newPosition = path->nodes.back().position +
				(path->nodes.back().position - path->nodes[path->nodes.size() - 2].position);
		}
		else
			return PathEditSelectionError(
				"path.edit_endpoint_required", "The active path element is not an endpoint node.");

		const Result<PathElementId> extended = ExtendScenePathEnd(
			MakeWindowPathEditContext(windowState), pathId, end, newPosition);
		if (extended)
			windowState.pathEdit.SetSelection({extended.Value()});
		return extended;
	}

	Result<PathElementId> InsertSelectedScenePathSegment(RendererWindowState &windowState)
	{
		if (!windowState.pathEdit.IsActive() || windowState.paths == nullptr ||
			windowState.pathEdit.ElementMode() != PathElementMode::Segment)
			return PathEditSelectionError(
				"path.edit_segment_required", "Select a segment in Path Edit Mode.");

		const SceneObjectId pathId = windowState.pathEdit.Path();
		const ScenePath *path = windowState.paths->Store().Find(pathId);
		if (path == nullptr)
			return PathEditSelectionError(
				"path.edit_segment_required", "The edited path does not exist.");
		const PathElementId selected = windowState.pathEdit.ActiveElement();
		const auto found = std::find_if(
			path->segments.begin(), path->segments.end(),
			[selected](const PathSegment &segment) { return segment.id == selected; });
		if (found == path->segments.end())
			return PathEditSelectionError(
				"path.edit_segment_required", "The active path element is not a segment.");

		const std::size_t index = static_cast<std::size_t>(found - path->segments.begin());
		const Result<PathElementId> inserted = InsertScenePathNode(
			MakeWindowPathEditContext(windowState), pathId, index, 0.5);
		if (inserted)
		{
			windowState.pathEdit.SetElementMode(PathElementMode::NodeHandle);
			windowState.pathEdit.SetSelection({inserted.Value()});
		}
		return inserted;
	}

	Result<void> DeleteSelectedScenePathNodes(RendererWindowState &windowState)
	{
		if (!windowState.pathEdit.IsActive() || windowState.paths == nullptr ||
			windowState.pathEdit.ElementMode() != PathElementMode::NodeHandle)
			return PathEditSelectionError(
				"path.edit_node_required", "Select one or more path nodes in Path Edit Mode.");

		const SceneObjectId pathId = windowState.pathEdit.Path();
		const ScenePath *path = windowState.paths->Store().Find(pathId);
		if (path == nullptr)
			return PathEditSelectionError(
				"path.edit_node_required", "The edited path does not exist.");
		std::vector<PathElementId> nodes;
		for (const PathElementId selected : windowState.pathEdit.Selection())
			if (std::any_of(path->nodes.begin(), path->nodes.end(),
				[selected](const PathNode &node) { return node.id == selected; }))
				nodes.push_back(selected);
		if (nodes.empty())
			return PathEditSelectionError(
				"path.edit_node_required", "The selection contains no path nodes.");

		const Result<void> deleted = DeleteScenePathNodes(
			MakeWindowPathEditContext(windowState), pathId, nodes);
		if (deleted)
			if (const ScenePath *edited = windowState.paths->Store().Find(pathId); edited != nullptr)
				windowState.pathEdit.PruneSelection(*edited);
		return deleted;
	}

	Result<void> SetSelectedScenePathHandleType(
		RendererWindowState &windowState,
		const BezierHandleType type)
	{
		if (!windowState.pathEdit.IsActive() || windowState.paths == nullptr ||
			windowState.pathEdit.ElementMode() != PathElementMode::NodeHandle)
			return PathEditSelectionError(
				"path.edit_handle_required", "Select one or more Bezier handles in Path Edit Mode.");

		const SceneObjectId pathId = windowState.pathEdit.Path();
		const ScenePath *path = windowState.paths->Store().Find(pathId);
		if (path == nullptr)
			return PathEditSelectionError(
				"path.edit_handle_required", "The edited path does not exist.");
		std::vector<PathElementId> handles;
		for (const PathElementId selected : windowState.pathEdit.Selection())
			if (std::any_of(path->segments.begin(), path->segments.end(), [selected](const PathSegment &segment) {
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data);
				return cubic != nullptr &&
					(cubic->startHandle.id == selected || cubic->endHandle.id == selected);
			}))
				handles.push_back(selected);
		if (handles.empty())
			return PathEditSelectionError(
				"path.edit_handle_required", "The selection contains no Bezier handles.");

		return SetScenePathHandleTypes(
			MakeWindowPathEditContext(windowState), pathId, handles, type);
	}

	Result<void> ReverseEditedScenePath(RendererWindowState &windowState)
	{
		if (!windowState.pathEdit.IsActive() || windowState.paths == nullptr)
			return PathEditSelectionError(
				"path.edit_path_required", "Open a path in Path Edit Mode.");
		const SceneObjectId pathId = windowState.pathEdit.Path();
		if (windowState.paths->Store().Find(pathId) == nullptr)
			return PathEditSelectionError(
				"path.edit_path_required", "The edited path does not exist.");

		const PathEditReport report = ReverseScenePaths(
			MakeWindowPathEditContext(windowState), std::span<const SceneObjectId>(&pathId, 1));
		if (!report.AnyApplied())
			return report.skipped.front().reason;
		if (const ScenePath *edited = windowState.paths->Store().Find(pathId); edited != nullptr)
			windowState.pathEdit.PruneSelection(*edited);
		return {};
	}

	Result<PathArcParameters> ResolveSelectedScenePathArc(const RendererWindowState &windowState)
	{
		if (!windowState.pathEdit.IsActive() || windowState.paths == nullptr ||
			windowState.pathEdit.ElementMode() != PathElementMode::Segment)
			return PathEditSelectionError(
				"path.edit_arc_required", "Select an arc segment in Path Edit Mode.");

		const ScenePath *path = windowState.paths->Store().Find(windowState.pathEdit.Path());
		if (path == nullptr)
			return PathEditSelectionError(
				"path.edit_arc_required", "The edited path does not exist.");
		const PathElementId selected = windowState.pathEdit.ActiveElement();
		const auto found = std::find_if(
			path->segments.begin(), path->segments.end(),
			[selected](const PathSegment &segment) { return segment.id == selected; });
		if (found == path->segments.end())
			return PathEditSelectionError(
				"path.edit_arc_required", "The active path element is not a segment.");
		const auto *arc = std::get_if<CircularArcSegmentData>(&found->data);
		if (arc == nullptr)
			return PathEditSelectionError(
				"path.edit_arc_required", "The active path segment is not a circular arc.");

		const std::size_t index = static_cast<std::size_t>(found - path->segments.begin());
		if (index + 1 >= path->nodes.size())
			return PathEditSelectionError(
				"path.edit_arc_required", "The active arc has no endpoint pair.");
		const Result<ArcGeometry> derived = DeriveArc(
			glm::dvec3(path->nodes[index].position),
			glm::dvec3(path->nodes[index + 1].position),
			arc->planeNormal,
			arc->signedSweepRadians);
		if (!derived)
			return derived.Error();

		PathArcParameters parameters;
		parameters.center = derived->center;
		parameters.axis = derived->normal;
		parameters.radius = derived->radius;
		parameters.startAngleRadians = derived->startAngle;
		parameters.signedSweepRadians = derived->signedSweep;
		return parameters;
	}

	Result<void> ApplySelectedScenePathArc(
		RendererWindowState &windowState,
		const PathArcParameters &parameters)
	{
		const Result<PathArcParameters> selected = ResolveSelectedScenePathArc(windowState);
		if (!selected)
			return selected.Error();
		return SetScenePathArcGeometry(
			MakeWindowPathEditContext(windowState),
			windowState.pathEdit.Path(),
			windowState.pathEdit.ActiveElement(),
			parameters);
	}
} // namespace DefectStudio
