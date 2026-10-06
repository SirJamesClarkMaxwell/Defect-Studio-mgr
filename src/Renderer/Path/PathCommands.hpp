#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	enum class PathEnd;
	struct PathArcParameters;

	// Which of the store's two counters an edit invalidates. Passing the wrong one is not a
	// correctness bug in the store - it is a stale-mesh bug three layers away - so the caller says it
	// once, at the operation, instead of every cache site guessing.
	enum class PathRevisionKind
	{
		Geometry,
		Style,
	};

	// Where an edit's undo entry goes. Injected rather than reached through the renderer globals
	// (`g_RendererUndoStack`, `g_RendererLayer`) so the whole op layer is testable with a local
	// UndoStack, and so a headless caller can run the same operations with no history at all.
	// `before` is the window's scene objects as they were immediately prior to the edit.
	using PathUndoSink =
		std::function<void(RendererWindowState &window, SceneObjectsSnapshot before, const std::string &description)>;

	// An empty `pushUndo` is legal and means "apply the edit, record no history" - that is what an
	// export preview or a migration pass wants, and it keeps the null case out of every operation.
	struct PathEditContext
	{
		RendererWindowState *window = nullptr;
		PathUndoSink pushUndo;
	};

	struct PathEditSkip
	{
		SceneObjectId path;
		StructuredError reason;
	};

	// A multi-path operation is not all-or-nothing across the set: it applies to every target that
	// validates and names the rest. Per target it IS atomic - see ApplyPathEdit.
	struct PathEditReport
	{
		std::vector<SceneObjectId> applied;
		std::vector<PathEditSkip> skipped;

		[[nodiscard]] bool AnyApplied() const
		{
			return !applied.empty();
		}
	};

	// The one engine every path edit goes through.
	//
	// For each target: copy the stored path, run `apply` on the copy, and keep the copy only if
	// `apply` succeeded AND ValidatePath finds nothing wrong with the result. Working on a copy is
	// what makes atomicity structural instead of a convention every future operation has to
	// remember - a rejected edit cannot have half-written the stored path, because it never touched
	// it. Committing happens only after the whole target set has been tried, so one late rejection
	// cannot leave the set half-edited either.
	//
	// The undo entry is pushed once, before the first commit, and only when at least one target
	// passed: an operation that changed nothing leaves no history and fires no dirty event.
	[[nodiscard]] PathEditReport ApplyPathEdit(
		const PathEditContext &context,
		std::span<const SceneObjectId> targets,
		PathRevisionKind revision,
		const std::string &description,
		const std::function<Result<void>(ScenePath &)> &apply);

	// The v2 operation list. Each one is a thin wrapper over ApplyPathEdit plus the validation that
	// belongs to it; the single-path forms return the skip reason as their error rather than a
	// report with one entry.
	//
	// The id comes from the window's SceneRegistry, exactly as for every other scene object.
	[[nodiscard]] Result<SceneObjectId> AddScenePath(const PathEditContext &context, ScenePath path);
	[[nodiscard]] PathEditReport DeleteScenePaths(const PathEditContext &context, std::span<const SceneObjectId> targets);
	[[nodiscard]] PathEditReport ReverseScenePaths(const PathEditContext &context, std::span<const SceneObjectId> targets);

	[[nodiscard]] Result<PathElementId> InsertScenePathNode(
		const PathEditContext &context, SceneObjectId path, std::size_t segment, double t);
	[[nodiscard]] Result<std::vector<PathElementId>> InsertScenePathNodes(
		const PathEditContext &context, SceneObjectId path, std::size_t segment, std::size_t count);
	[[nodiscard]] Result<PathElementId> ExtendScenePathEnd(
		const PathEditContext &context, SceneObjectId path, PathEnd end, glm::vec3 newPosition);
	[[nodiscard]] Result<void> DeleteScenePathNode(const PathEditContext &context, SceneObjectId path, PathElementId node);
	[[nodiscard]] Result<void> DeleteScenePathNodes(
		const PathEditContext &context, SceneObjectId path, std::span<const PathElementId> nodes);

	[[nodiscard]] Result<void> MoveScenePathNode(
		const PathEditContext &context, SceneObjectId path, PathElementId node, glm::vec3 position);
	[[nodiscard]] Result<void> MoveScenePathHandle(
		const PathEditContext &context, SceneObjectId path, PathElementId handle, glm::vec3 offset);
	[[nodiscard]] Result<void> SetScenePathHandleType(
		const PathEditContext &context, SceneObjectId path, PathElementId handle, BezierHandleType type);
	[[nodiscard]] Result<void> SetScenePathHandleTypes(
		const PathEditContext &context,
		SceneObjectId path,
		std::span<const PathElementId> handles,
		BezierHandleType type);

	// Sets the two fields an arc segment actually stores, after checking that they describe a real
	// arc between the segment's existing endpoints (DeriveArc). The numeric editor that moves the
	// endpoints to satisfy an authored centre and radius is S13; nothing here moves a node.
	[[nodiscard]] Result<void> SetScenePathArcParameters(
		const PathEditContext &context,
		SceneObjectId path,
		PathElementId segment,
		glm::vec3 planeNormal,
		float signedSweepRadians);
	[[nodiscard]] Result<void> SetScenePathArcGeometry(
		const PathEditContext &context,
		SceneObjectId path,
		PathElementId segment,
		const PathArcParameters &parameters);

	// One entry point instead of a Set<Field>Style per field: style has no cross-field invariant the
	// store can check, so a per-field API would be the same body copied a dozen times.
	[[nodiscard]] PathEditReport SetScenePathStyle(
		const PathEditContext &context,
		std::span<const SceneObjectId> targets,
		const std::function<void(PathStrokeStyle &)> &mutate);

	[[nodiscard]] Result<void> SetScenePathBinding(
		const PathEditContext &context, SceneObjectId path, PathElementId node, PathBinding binding);
	[[nodiscard]] Result<void> DetachScenePathBinding(
		const PathEditContext &context, SceneObjectId path, PathElementId node);

	// One undo entry for a whole drag, and none at all for a cancelled one.
	//
	// The captured state is the window's entire scene objects, not just its paths, because a drag
	// can be part of a mixed modal transform: undoing it has to put the atoms and the labels back
	// where they were too, in the same single step.
	class PathDragTransaction
	{
	public:
		void Begin(PathEditContext context, std::string description);
		[[nodiscard]] bool Active() const noexcept;

		// Applies live, never touches the undo stack. On an inactive transaction it applies nothing
		// and reports every target as skipped - a stray Update after a Commit is a caller bug, not a
		// reason to write an untracked edit into the store.
		[[nodiscard]] PathEditReport Update(
			std::span<const SceneObjectId> targets,
			PathRevisionKind revision,
			const std::function<Result<void>(ScenePath &)> &apply);

		// Pushes exactly one undo entry if any Update changed anything, then ends the transaction.
		void Commit();

		// Restores the exact pre-Begin state and pushes nothing.
		void Cancel();

	private:
		PathEditContext m_Context;
		std::optional<SceneObjectsSnapshot> m_Before;
		std::string m_Description;
		bool m_Changed = false;
		bool m_Active = false;
	};
} // namespace DefectStudio
