#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/ScenePathPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransform.hpp"
#include "../../IO/SceneObjectsV1Fixtures.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig EmptyConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	}

	class ScenePathCutoverCommandTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undo);
			RegisterViewportSceneObjectCommands(registry, renderer);
		}
		void TearDown() override { renderer.OnDetach(); }
		Ref<UndoStack> undo = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyConfig()};
		CommandRegistry registry;
	};

	TEST_F(ScenePathCutoverCommandTests, ReverseSelectedPathsIsOneUndoAndLeavesUnselectedPath)
	{
		RendererWindowState window;
		window.windowId = "cutover";
		const ScenePath original = MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0));
		const auto first = SceneSystem::AppendScenePath(window, original);
		const auto second = SceneSystem::AppendScenePath(window, original);
		const auto third = SceneSystem::AppendScenePath(window, original);
		window.selectedScenePaths = {first, second};
		renderer.AddWindow(std::move(window));
		auto &live = renderer.GetWindows().front();

		ASSERT_TRUE(registry.Execute(CommandID{"renderer.scene_path.reverse"}));
		for (const auto id : {first, second})
		{
			const ScenePath &reversed = *live.paths->Store().Find(id);
			EXPECT_EQ(reversed.nodes.front().position, original.nodes.back().position);
			EXPECT_EQ(reversed.nodes.back().position, original.nodes.front().position);
			EXPECT_EQ(reversed.style.startDecoration.kind, PathDecorationKind::Arrow);
			EXPECT_EQ(reversed.style.endDecoration.kind, PathDecorationKind::None);
		}
		EXPECT_EQ(live.paths->Store().Find(third)->nodes.front().position, original.nodes.front().position);
		ASSERT_EQ(undo->GetUndoDepth(), 1u);
		ASSERT_TRUE(undo->Undo());
		for (const auto id : {first, second, third})
			EXPECT_EQ(live.paths->Store().Find(id)->nodes.front().position, original.nodes.front().position);
		ASSERT_TRUE(undo->Redo());
		EXPECT_EQ(live.paths->Store().Find(first)->nodes.front().position, original.nodes.back().position);
	}

	TEST_F(ScenePathCutoverCommandTests, EmptyPathSelectionPushesNoUndo)
	{
		RendererWindowState window;
		window.windowId = "empty";
		renderer.AddWindow(std::move(window));
		ASSERT_TRUE(registry.Execute(CommandID{"renderer.scene_path.reverse"}));
		EXPECT_EQ(undo->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathCutoverCommandTests, PathDeleteThroughSharedDispatcherRecordsOneUndo)
	{
		RendererWindowState window;
		window.windowId = "delete";
		const auto id = SceneSystem::AppendScenePath(window,
			MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0)));
		window.selectedScenePaths = {id};
		renderer.AddWindow(std::move(window));
		auto &live = renderer.GetWindows().front();
		ASSERT_TRUE(ExecuteSceneObjectEditAction(live, SceneObjectEditKind::Path, SceneObjectEditAction::Delete));
		EXPECT_TRUE(live.paths->Store().Empty());
		ASSERT_EQ(undo->GetUndoDepth(), 1u);
		ASSERT_TRUE(undo->Undo());
		EXPECT_TRUE(live.paths->Store().Contains(id));
	}

	TEST(ScenePathCutoverTests, DrawThroughAtomsCreatesTwoCopyPositionBindings)
	{
		for (const bool arrow : {false, true})
		{
			RendererWindowState window;
			window.structure.atoms = {{"C", glm::vec3(1, 2, 3)}, {"N", glm::vec3(4, 2, 3)}};
			window.selectedAtomIndices = {1, 0};
			const auto added = AddScenePathThroughSelectedAtoms(window, arrow);
			ASSERT_TRUE(added);
			const auto &path = *window.paths->Store().Find(added.Value());
			ASSERT_EQ(path.nodes.size(), 2u);
			ASSERT_EQ(path.segments.size(), 1u);
			EXPECT_TRUE(std::holds_alternative<LineSegmentData>(path.segments[0].data));
			for (std::size_t index = 0; index < 2; ++index)
			{
				ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(path.nodes[index].binding.value));
				const auto &binding = std::get<PathBinding::CopyPosition>(path.nodes[index].binding.value);
				EXPECT_EQ(binding.atomIndex, 1u - index);
				EXPECT_FLOAT_EQ(binding.buffer, GetScenePathAtomBuffer());
				EXPECT_EQ(binding.offset, glm::vec3(0));
			}
			EXPECT_EQ(path.style.endDecoration.kind, arrow ? PathDecorationKind::Arrow : PathDecorationKind::None);
			EXPECT_EQ(window.selectedScenePaths, (std::vector<SceneObjectId>{added.Value()}));
			EXPECT_TRUE(window.selectedAtomIndices.empty());
		}
	}

	TEST(ScenePathCutoverTests, DrawThroughAtomsRejectsInvalidSelectionWithoutCreatingObjects)
	{
		RendererWindowState window;
		window.structure.atoms = {{"C", glm::vec3(0)}};
		window.selectedAtomIndices = {0, 9};
		EXPECT_FALSE(AddScenePathThroughSelectedAtoms(window, true));
		EXPECT_TRUE(window.paths == nullptr || window.paths->Store().Empty());
	}

	TEST(ScenePathCutoverTests, FreeLineAndArrowUseSceneRelativeLengthAndFreeNodes)
	{
		for (const bool arrow : {false, true})
			for (const float extent : {0.0f, 1.0f, 10.0f, 100.0f})
			{
				RendererWindowState window;
				if (extent > 0)
					window.structure.atoms = {{"C", glm::vec3(0)}, {"C", glm::vec3(extent, 0, 0)}};
				const glm::vec3 seed(2, 3, 4);
				const float expectedLength = extent > 0.0f ? std::clamp(extent * 0.20f, 0.75f, 4.0f) : 1.0f;
				const auto added = AddFreeScenePathSegment(window, seed, arrow);
				ASSERT_TRUE(added);
				const auto &path = *window.paths->Store().Find(added.Value());
				ASSERT_EQ(path.nodes.size(), 2u);
				for (const auto &node : path.nodes)
					EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(node.binding.value));
				EXPECT_EQ(path.transform.position + path.nodes.front().position, seed);
				EXPECT_EQ(path.transform.position + path.nodes.back().position, seed + glm::vec3(expectedLength, 0, 0));
				EXPECT_EQ(path.style.endDecoration.kind, arrow ? PathDecorationKind::Arrow : PathDecorationKind::None);
				EXPECT_EQ(window.selectedScenePaths, (std::vector<SceneObjectId>{added.Value()}));
			}
	}

	TEST(ScenePathCutoverTests, LoadingV1ArrowsCreatesOnlyPathsAndSavingReopensAsPaths)
	{
		for (const char *fixture : {SceneObjectsV1Fixtures::Line, SceneObjectsV1Fixtures::Arrow3D,
			SceneObjectsV1Fixtures::Arrow2DBillboard, SceneObjectsV1Fixtures::Arrow2DFixedPlane,
			SceneObjectsV1Fixtures::Quadratic, SceneObjectsV1Fixtures::TipsGradientOutline})
		{
			SceneObjectsFile file;
			std::string error;
			std::vector<StructuredError> warnings;
			ASSERT_TRUE(SceneObjectsIO::Parse(fixture, file, warnings, error)) << error;
			RendererWindowState window;
			ApplyPersistedSceneObjects(window, file.structures[0].objects, warnings);
			ASSERT_NE(window.paths, nullptr);
			EXPECT_EQ(window.paths->Store().Size(), 1u);
			file.structures[0].objects = ExtractPersistedSceneObjects(window);
			ASSERT_EQ(file.structures[0].objects.size(), 1u);
			EXPECT_TRUE(std::holds_alternative<PersistedScenePath>(file.structures[0].objects[0]));
			const std::string saved = SceneObjectsIO::Serialize(file);
			EXPECT_EQ(saved.find("SceneArrow"), std::string::npos);
			SceneObjectsFile reopened;
			ASSERT_TRUE(SceneObjectsIO::Parse(saved, reopened, warnings, error)) << error;
			RendererWindowState target;
			ApplyPersistedSceneObjects(target, reopened.structures[0].objects, warnings);
			ASSERT_NE(target.paths, nullptr);
			EXPECT_EQ(target.paths->Store().Size(), 1u);
		}
	}

	TEST(ScenePathCutoverTests, SavingExtractsOnlyTheCreatedPath)
	{
		RendererWindowState window;
		ASSERT_TRUE(AddFreeScenePathSegment(window, glm::vec3(0), true));
		const auto saved = ExtractPersistedSceneObjects(window);
		ASSERT_EQ(saved.size(), 1u);
		EXPECT_TRUE(std::holds_alternative<PersistedScenePath>(saved[0]));
	}

	TEST(ScenePathCutoverTests, SavingMigratesUnopenedV1EntriesAndPreservesReferences)
	{
		SceneObjectsFile file;
		PersistedSceneArrow arrow;
		arrow.persistKey = "unopened";
		arrow.startAnchorAtoms = {{42, "C", glm::vec3(3, 4, 5)}};
		arrow.atomBuffer = 0.75f;
		file.structures.push_back({"unopened", {arrow}});
		file.projectObjects = {arrow};
		std::vector<StructuredError> warnings;
		ASSERT_TRUE(MigratePersistedSceneArrows(file, warnings));
		const auto &path = std::get<PersistedScenePath>(file.structures[0].objects[0]);
		EXPECT_EQ(path.persistKey, "unopened");
		ASSERT_EQ(path.nodes[0].binding.atoms.size(), 1u);
		EXPECT_EQ(path.nodes[0].binding.atoms[0].index, 42u);
		EXPECT_EQ(path.nodes[0].binding.atoms[0].element, "C");
		EXPECT_EQ(path.nodes[0].binding.atoms[0].position, glm::vec3(3, 4, 5));
		EXPECT_FLOAT_EQ(path.nodes[0].binding.buffer, 0.75f);
		EXPECT_EQ(SceneObjectsIO::Serialize(file).find("SceneArrow"), std::string::npos);
	}

	TEST(ScenePathCutoverTests, SaveMigrationRejectsInvalidArrowsWithoutChangingTheFile)
	{
		SceneObjectsFile file;
		PersistedSceneArrow invalid;
		invalid.points.clear();
		file.projectObjects = {PersistedSceneArrow{}};
		file.structures.push_back({"invalid", {invalid}});
		const auto before = SceneObjectsIO::Serialize(file);
		std::vector<StructuredError> warnings;
		EXPECT_FALSE(MigratePersistedSceneArrows(file, warnings));
		EXPECT_EQ(SceneObjectsIO::Serialize(file), before);
	}

	TEST(ScenePathCutoverTests, MigrationWarningsAndInvalidArrowAreReported)
	{
		RendererWindowState window;
		PersistedSceneArrow dropped;
		dropped.style.outlineWidth = 1.0f;
		PersistedSceneArrow invalid;
		invalid.points[1].x = std::numeric_limits<float>::quiet_NaN();
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {dropped, invalid}, warnings);
		ASSERT_NE(window.paths, nullptr);
		EXPECT_EQ(window.paths->Store().Size(), 1u);
		EXPECT_GE(warnings.size(), 2u);
	}

	TEST(ScenePathCutoverTests, MigrationRebindsReorderedAtomsAndDetachesMissingAnchor)
	{
		RendererWindowState window;
		window.structure.atoms = {{"N", glm::vec3(2, 0, 0)}, {"C", glm::vec3(0)}};
		PersistedSceneArrow arrow;
		arrow.points = {glm::vec3(0), glm::vec3(2, 0, 0)};
		arrow.startAnchorAtoms = {{0, "C", glm::vec3(0)}};
		arrow.endAnchorAtoms = {{1, "O", glm::vec3(2, 0, 0)}};
		arrow.atomBuffer = 0.75f;
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {arrow}, warnings);
		const auto saved = ExtractPersistedSceneObjects(window);
		ASSERT_EQ(saved.size(), 1u);
		const auto &path = std::get<PersistedScenePath>(saved[0]);
		EXPECT_EQ(path.nodes[0].binding.kind, "CopyPosition");
		ASSERT_EQ(path.nodes[0].binding.atoms.size(), 1u);
		EXPECT_EQ(path.nodes[0].binding.atoms[0].index, 1u);
		EXPECT_FLOAT_EQ(path.nodes[0].binding.buffer, 0.75f);
		EXPECT_EQ(path.nodes[1].binding.kind, "Free");
		EXPECT_FALSE(warnings.empty());
	}

	TEST(ScenePathCutoverTests, DefectAxesCarryPinnedPathOnceAndRestoreItsTransform)
	{
		RendererWindowState window;
		const auto added = AddFreeScenePathSegment(window, glm::vec3(2, 0, 0), false);
		ASSERT_TRUE(added);
		window.structure.defectFrame = DefectFrame{};
		ParentSelectionToDefectFrame(window);
		ParentSelectionToDefectFrame(window);
		EXPECT_EQ(window.defectFrameChildren.paths, (std::vector<SceneObjectId>{added.Value()}));
		EXPECT_EQ(window.defectFrameChildren.Count(), 1u);
		window.defectFrameSelected = true;
		const auto before = window.paths->Store().Find(added.Value())->transform;
		const auto snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.paths.size(), 1u);
		window.selectedScenePaths.clear();
		const auto onlyFrame = CaptureSceneTransformSelection(window);
		ASSERT_EQ(onlyFrame.paths.size(), 1u);
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(1, 2, 3);
		ApplySceneTransformSelection(window, onlyFrame, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(0));
		EXPECT_EQ(window.paths->Store().Find(added.Value())->transform.position, before.position + delta.spatial.translation);
		RestoreSceneTransformSelection(window, onlyFrame);
		EXPECT_EQ(window.paths->Store().Find(added.Value())->transform.position, before.position);
	}
}
