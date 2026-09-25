#include <gtest/gtest.h>

#include <array>
#include <limits>

#include <glm/gtc/constants.hpp>

#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakeLine(const SceneObjectId id, const float y = 0.0f)
		{
			ScenePath path;
			path.id = id;
			path.name = "path";
			path.nodes = {{AllocateElementId(path), {0.0f, y, 0.0f}, {}}, {AllocateElementId(path), {2.0f, y, 0.0f}, {}}};
			path.segments.push_back({AllocateElementId(path), LineSegmentData{}});
			return path;
		}

		ScenePath MakeCubic(const SceneObjectId id)
		{
			ScenePath path = MakeLine(id);
			CubicBezierSegmentData cubic;
			cubic.startHandle = {AllocateElementId(path), {0.5f, 1.0f, 0.0f}, BezierHandleType::Free};
			cubic.endHandle = {AllocateElementId(path), {1.5f, 1.0f, 0.0f}, BezierHandleType::Free};
			path.segments[0].data = cubic;
			return path;
		}

		ScenePath MakeArc(const SceneObjectId id)
		{
			ScenePath path = MakeLine(id);
			path.segments[0].data = CircularArcSegmentData{{0.0f, 0.0f, 1.0f}, glm::half_pi<float>()};
			return path;
		}

		RendererWindowState MakeWindow()
		{
			RendererWindowState window;
			window.windowId = "path-tests";
			return window;
		}

		SceneObjectsWindowResolver ResolverFor(RendererWindowState &window)
		{
			return [&window](const std::string &id) { return id == window.windowId ? &window : nullptr; };
		}

		PathUndoSink SinkFor(RendererWindowState &window, UndoStack &stack, int &calls)
		{
			return [&window, &stack, &calls](RendererWindowState &, SceneObjectsSnapshot before, const std::string &description) {
				++calls;
				EXPECT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
					ResolverFor(window), window.windowId, std::move(before), {}, description)));
			};
		}

		void InsertPath(RendererWindowState &window, ScenePath path)
		{
			ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(std::move(path)));
		}

		PathEditContext Context(RendererWindowState &window, UndoStack &stack, int &calls)
		{
			return {&window, SinkFor(window, stack, calls)};
		}
	} // namespace

	TEST(PathCommandsTests, ApplyValidEditMutatesAndReportsApplied)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeLine({1}));
		const std::array ids{SceneObjectId{1}};
		const PathEditReport report = ApplyPathEdit(Context(window, stack, calls), ids, PathRevisionKind::Geometry, "move", [](ScenePath &path) {
			path.nodes[0].position.x = 1.0f;
			return Result<void>{};
		});
		ASSERT_EQ(report.applied, std::vector<SceneObjectId>{SceneObjectId{1}});
		EXPECT_TRUE(report.skipped.empty());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.x, 1.0f);
	}

	TEST(PathCommandsTests, ApplyEditSkipsFailedAndInvalidCopiesAtomically)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeLine({1}, 1.0f));
		InsertPath(window, MakeLine({2}, 2.0f));
		const std::array ids{SceneObjectId{1}, SceneObjectId{2}};
		const PathEditReport rejected = ApplyPathEdit(Context(window, stack, calls), ids, PathRevisionKind::Geometry, "reject", [](ScenePath &path) {
			if (path.id == SceneObjectId{1})
				return Result<void>(StructuredError{ErrorCategory::Validation, Severity::Error, "rejected", "test", "", "test", "path.test_rejected"});
			path.segments.clear();
			return Result<void>{};
		});
		EXPECT_TRUE(rejected.applied.empty());
		ASSERT_EQ(rejected.skipped.size(), 2u);
		EXPECT_EQ(rejected.skipped[0].reason.code, "path.test_rejected");
		EXPECT_EQ(rejected.skipped[1].reason.code, "path.edit_invalid_result");
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.y, 1.0f);
		EXPECT_EQ(window.paths->Store().Find({2})->segments.size(), 1u);
		EXPECT_EQ(calls, 0);
		EXPECT_FALSE(stack.CanUndo());
	}

	TEST(PathCommandsTests, SuccessfulEditUsesOneUndoEntryAndRevisionKind)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeLine({1}));
		const PathRevisions before = window.paths->Store().RevisionsFor({1});
		const std::array ids{SceneObjectId{1}};
		ASSERT_TRUE(ApplyPathEdit(Context(window, stack, calls), ids, PathRevisionKind::Style, "style", [](ScenePath &path) {
			path.style.width = 0.2f;
			return Result<void>{};
		}).AnyApplied());
		const PathRevisions after = window.paths->Store().RevisionsFor({1});
		EXPECT_EQ(after.geometry, before.geometry);
		EXPECT_EQ(after.style, before.style + 1);
		EXPECT_EQ(calls, 1);
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->style.width, PathStrokeStyle{}.width);
		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->style.width, 0.2f);
	}

	TEST(PathCommandsTests, AddAndDeleteUseSceneIdsAndUndoSnapshots)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		const Result<SceneObjectId> added = AddScenePath(Context(window, stack, calls), MakeLine({99}));
		ASSERT_TRUE(added);
		EXPECT_EQ(added.Value(), SceneObjectId{1});
		EXPECT_TRUE(window.paths->Store().Contains(added.Value()));
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_TRUE(window.paths->Store().Empty());
		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_TRUE(window.paths->Store().Contains(added.Value()));

		const Result<SceneObjectId> addedTwo = AddScenePath(Context(window, stack, calls), MakeLine({100}, 2.0f));
		ASSERT_TRUE(addedTwo);
		const std::array ids{added.Value(), addedTwo.Value(), SceneObjectId{900}};
		const PathEditReport deleted = DeleteScenePaths(Context(window, stack, calls), ids);
		EXPECT_EQ(deleted.applied.size(), 2u);
		ASSERT_EQ(deleted.skipped.size(), 1u);
		EXPECT_EQ(deleted.skipped[0].path, SceneObjectId{900});
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_EQ(window.paths->Store().Size(), 2u);
		EXPECT_FLOAT_EQ(window.paths->Store().Find(addedTwo.Value())->nodes[0].position.y, 2.0f);
	}

	TEST(PathCommandsTests, TopologyOperationsAreUndoable)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeLine({1}));
		const std::array ids{SceneObjectId{1}};
		ASSERT_TRUE(ReverseScenePaths(Context(window, stack, calls), ids).AnyApplied());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.x, 2.0f);
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.x, 0.0f);

		const Result<PathElementId> inserted = InsertScenePathNode(Context(window, stack, calls), {1}, 0, 0.5);
		ASSERT_TRUE(inserted);
		EXPECT_EQ(window.paths->Store().Find({1})->nodes.size(), 3u);
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_EQ(window.paths->Store().Find({1})->nodes.size(), 2u);
	}

	TEST(PathCommandsTests, NodeMoveRejectsNonFiniteAndBindingIsUndoable)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeLine({1}));
		const PathElementId node = window.paths->Store().Find({1})->nodes[0].id;
		const Result<void> invalid = MoveScenePathNode(Context(window, stack, calls), {1}, node, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
		EXPECT_FALSE(invalid);
		EXPECT_EQ(invalid.Error().code, "path.edit_non_finite");
		EXPECT_EQ(calls, 0);
		EXPECT_FALSE(stack.CanUndo());

		PathBinding::CopyPosition binding;
		binding.atomIndex = 3;
		ASSERT_TRUE(SetScenePathBinding(Context(window, stack, calls), {1}, node, PathBinding{binding}));
		EXPECT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(window.paths->Store().Find({1})->nodes[0].binding.value));
		ASSERT_TRUE(DetachScenePathBinding(Context(window, stack, calls), {1}, node));
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(window.paths->Store().Find({1})->nodes[0].binding.value));
	}

	// The handle wrappers reach a lookup the node wrappers never touch: a handle lives inside a
	// segment's variant, not in path.nodes, so an untested FindHandle is an untested code path.
	TEST(PathCommandsTests, HandleEditsResolveHandlesInsideSegments)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeCubic({1}));
		const auto &cubic = std::get<CubicBezierSegmentData>(window.paths->Store().Find({1})->segments[0].data);
		const PathElementId handle = cubic.startHandle.id;

		EXPECT_FALSE(MoveScenePathHandle(Context(window, stack, calls), {1}, handle, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}));
		EXPECT_FALSE(MoveScenePathHandle(Context(window, stack, calls), {1}, {9999}, {0.0f, 0.0f, 0.0f}));
		EXPECT_EQ(calls, 0);

		ASSERT_TRUE(MoveScenePathHandle(Context(window, stack, calls), {1}, handle, {0.5f, 4.0f, 0.0f}));
		ASSERT_TRUE(SetScenePathHandleType(Context(window, stack, calls), {1}, handle, BezierHandleType::Vector));
		const auto &edited = std::get<CubicBezierSegmentData>(window.paths->Store().Find({1})->segments[0].data);
		EXPECT_FLOAT_EQ(edited.startHandle.position.y, 4.0f);
		EXPECT_EQ(edited.startHandle.type, BezierHandleType::Vector);

		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_EQ(std::get<CubicBezierSegmentData>(window.paths->Store().Find({1})->segments[0].data).startHandle.type, BezierHandleType::Free);
	}

	TEST(PathCommandsTests, ArcParametersRequireAValidDerivedArc)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeArc({1}));
		const PathElementId segment = window.paths->Store().Find({1})->segments[0].id;
		EXPECT_FALSE(SetScenePathArcParameters(Context(window, stack, calls), {1}, segment, {0.0f, 0.0f, 1.0f}, 0.0f));
		EXPECT_FALSE(SetScenePathArcParameters(Context(window, stack, calls), {1}, segment, {1.0f, 0.0f, 0.0f}, glm::half_pi<float>()));
		EXPECT_TRUE(SetScenePathArcParameters(Context(window, stack, calls), {1}, segment, {0.0f, 0.0f, 1.0f}, glm::pi<float>()));
	}

	TEST(PathCommandsTests, DragCommitsOnceAndCancelRestoresWholeScene)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		int calls = 0;
		InsertPath(window, MakeLine({1}));
		RendererWindowState::FreeLabel label;
		label.text = "before";
		window.freeLabels.push_back(label);
		const PathElementId node = window.paths->Store().Find({1})->nodes[0].id;
		const std::array ids{SceneObjectId{1}};
		PathDragTransaction drag;
		drag.Begin(Context(window, stack, calls), "drag");
		ASSERT_TRUE(drag.Update(ids, PathRevisionKind::Geometry, [](ScenePath &path) {
			path.nodes[0].position.x = 1.0f;
			return Result<void>{};
		}).AnyApplied());
		ASSERT_TRUE(drag.Update(ids, PathRevisionKind::Geometry, [](ScenePath &path) {
			path.nodes[0].position.x = 2.0f;
			return Result<void>{};
		}).AnyApplied());
		ASSERT_TRUE(drag.Update(ids, PathRevisionKind::Geometry, [](ScenePath &path) {
			path.nodes[0].position.x = 3.0f;
			return Result<void>{};
		}).AnyApplied());
		window.freeLabels[0].text = "edited";
		drag.Commit();
		EXPECT_EQ(calls, 1);
		EXPECT_EQ(drag.Update(ids, PathRevisionKind::Geometry, [](ScenePath &) { return Result<void>{}; }).skipped.size(), 1u);
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.x, 0.0f);
		EXPECT_EQ(window.freeLabels[0].text, "before");
		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.x, 3.0f);
		EXPECT_EQ(window.freeLabels[0].text, "edited");

		PathDragTransaction cancelled;
		cancelled.Begin(Context(window, stack, calls), "cancel");
		ASSERT_TRUE(cancelled.Update(ids, PathRevisionKind::Geometry, [](ScenePath &path) {
			path.nodes[0].position.x = 8.0f;
			return Result<void>{};
		}).AnyApplied());
		cancelled.Cancel();
		EXPECT_FLOAT_EQ(window.paths->Store().Find({1})->nodes[0].position.x, 3.0f);
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
		EXPECT_EQ(cancelled.Update(ids, PathRevisionKind::Geometry, [](ScenePath &) { return Result<void>{}; }).skipped.size(), 1u);
	}

	TEST(PathCommandsTests, InactiveDragReportsEveryTargetAndChangesNothing)
	{
		RendererWindowState window = MakeWindow();
		PathDragTransaction drag;
		const std::array ids{SceneObjectId{1}, SceneObjectId{2}};
		const PathEditReport report = drag.Update(ids, PathRevisionKind::Geometry, [](ScenePath &) { return Result<void>{}; });
		EXPECT_EQ(report.skipped.size(), 2u);
		EXPECT_TRUE(report.applied.empty());
		EXPECT_EQ(report.skipped[0].reason.code, "path.edit_inactive_transaction");
	}
} // namespace DefectStudio::Tests
