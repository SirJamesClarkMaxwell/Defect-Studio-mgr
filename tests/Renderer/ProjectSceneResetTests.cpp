#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio::Tests
{
	TEST(ProjectSceneResetTests, UndoReplayMarksDirtyAndResetInvalidatesPreviousProjectHistory)
	{
		RendererStartupConfig config;
		config.loadDefaultScene = false;
		RendererLayer renderer{std::move(config)};
		auto undoStack = CreateRef<UndoStack>();
		renderer.BindUndoStack(undoStack);
		std::vector<StructuredError> warnings;
		PersistedFreeLabel label;
		label.persistKey = "first-project";
		label.text = "Original";
		PersistedScenePath path;
		path.persistKey = "first-project-path";
		path.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		path.segments.resize(1);
		auto &window = ResetProjectSceneWindow(renderer, {label, path}, warnings);
		PushPinnedMeasurementUndoSnapshot(window);
		window.freeLabels.front().text = "Edited";
		window.sceneObjectsDirty = false;
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_EQ(window.freeLabels.front().text, "Original");
		EXPECT_TRUE(window.sceneObjectsDirty);
		window.sceneObjectsDirty = false;
		ASSERT_TRUE(undoStack->Redo());
		EXPECT_EQ(window.freeLabels.front().text, "Edited");
		EXPECT_TRUE(window.sceneObjectsDirty);

		window.pathEdit.Enter(window.paths->Store().At(0)->id);
		window.selectedFreeLabels = {window.freeLabels.front().id};
		window.scenePathStyleEditBefore.emplace();
		window.modalTransformStartRequested = true;
		ResetProjectSceneWindow(renderer, {}, warnings);
		EXPECT_FALSE(window.pathEdit.IsActive());
		EXPECT_TRUE(window.selectedFreeLabels.empty());
		EXPECT_FALSE(window.scenePathStyleEditBefore.has_value());
		EXPECT_FALSE(window.modalTransformStartRequested);
		EXPECT_FALSE(window.sceneObjectsDirty);
		EXPECT_FALSE(undoStack->CanUndo());
		EXPECT_FALSE(undoStack->CanRedo());
		EXPECT_TRUE(GatherProjectSceneObjects(renderer).empty());
		EXPECT_TRUE(warnings.empty());
		renderer.OnDetach();
	}

	TEST(ProjectSceneResetTests, ProjectObjectsWithoutStructuresLoadAndNullIsRejected)
	{
		SceneObjectsFile file;
		PersistedFreeLabel label;
		label.persistKey = "project-label";
		file.projectObjects.push_back(label);
		const std::string serialized = SceneObjectsIO::Serialize(file);
		const std::string projectOnly = "formatVersion: 2\n" + serialized.substr(serialized.find("projectObjects:"));
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(projectOnly, loaded, warnings, error)) << error;
		EXPECT_TRUE(loaded.structures.empty());
		ASSERT_EQ(loaded.projectObjects.size(), 1u);
		EXPECT_EQ(std::get<PersistedFreeLabel>(loaded.projectObjects.front()).persistKey, "project-label");
		EXPECT_FALSE(SceneObjectsIO::Parse("projectObjects: null\n", loaded, warnings, error));
		EXPECT_NE(error.find("projectObjects"), std::string::npos);
	}
} // namespace DefectStudio::Tests
