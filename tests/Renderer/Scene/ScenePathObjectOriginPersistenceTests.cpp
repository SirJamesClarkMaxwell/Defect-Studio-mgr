#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <variant>
#include <vector>

#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath BoundPath(SceneObjectId target)
		{
			ScenePath path;
			path.nodes = {{AllocateElementId(path), {3, 4, 5}, PathBinding{PathBinding::ObjectOrigin{target, {1, 2, 3}}}},
				{AllocateElementId(path), {6, 4, 5}, {}}};
			path.segments = {{AllocateElementId(path), LineSegmentData{}}};
			return path;
		}

		PersistedScenePath SavedPath(const std::string &targetKey)
		{
			PersistedScenePath path;
			path.persistKey = "path";
			path.nodes = {{{3, 4, 5}}, {{6, 4, 5}}};
			path.segments = {{}};
			path.nodes[0].binding.kind = "ObjectOrigin";
			path.nodes[0].binding.objectPersistKey = targetKey;
			return path;
		}
	}

	TEST(ScenePathObjectOriginPersistenceTests, FreeLabelOriginRoundTripsThroughYamlWithNewObjectId)
	{
		RendererWindowState window;
		RendererWindowState::FreeLabel label;
		label.id = window.sceneRegistry.AllocateObjectId();
		label.text = "target";
		label.worldPosition = {8, 9, 10};
		window.freeLabels.push_back(label);
		const SceneObjectId pathId = SceneSystem::AppendScenePath(window, BoundPath(label.id));
		ASSERT_TRUE(pathId.IsValid());
		// Save must assign keys even when there has been no annotation sync yet.
		ASSERT_TRUE(window.freeLabels[0].persistKey.empty());
		const auto objects = ExtractPersistedSceneObjects(window);
		ASSERT_FALSE(window.freeLabels[0].persistKey.empty());
		ASSERT_EQ(objects.size(), 2u);
		const auto &savedPath = std::get<PersistedScenePath>(objects[1]);
		EXPECT_EQ(savedPath.nodes[0].binding.objectPersistKey, window.freeLabels[0].persistKey);
		SceneObjectsFile file;
		// Put the path first to exercise the forward-reference second pass.
		file.structures.push_back({"structure", {objects[1], objects[0]}});
		SceneObjectsFile parsed;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), parsed, warnings, error)) << error;
		ASSERT_EQ(parsed.structures.size(), 1u);
		ApplyPersistedSceneObjects(window, parsed.structures[0].objects, warnings);
		ASSERT_TRUE(warnings.empty());
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		ASSERT_EQ(window.freeLabels.size(), 1u);
		ASSERT_EQ(window.paths->Store().Size(), 1u);
		const ScenePath &loaded = *window.paths->Store().At(0);
		ASSERT_TRUE(std::holds_alternative<PathBinding::ObjectOrigin>(loaded.nodes[0].binding.value));
		const auto binding = std::get<PathBinding::ObjectOrigin>(loaded.nodes[0].binding.value);
		EXPECT_NE(window.freeLabels[0].id, label.id);
		EXPECT_EQ(binding.object, window.freeLabels[0].id);
		EXPECT_EQ(binding.offset, glm::vec3(1, 2, 3));
		EXPECT_EQ(ResolveNodePositions(loaded, SceneSystem::MakePathBindingContext(window)).positions[0], glm::vec3(9, 11, 13));
		window.freeLabels[0].worldPosition += glm::vec3(2, 0, 0);
		SceneSystem::UpdateLabelTransforms(window.sceneRegistry, window);
		EXPECT_EQ(ResolveNodePositions(loaded, SceneSystem::MakePathBindingContext(window)).positions[0], glm::vec3(11, 11, 13));
	}

	TEST(ScenePathObjectOriginPersistenceTests, UnknownKeyWarnsAndKeepsStoredPositionAsFree)
	{
		RendererWindowState window;
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {SavedPath("missing")}, warnings);
		ASSERT_EQ(window.paths->Store().Size(), 1u);
		const ScenePath &loaded = *window.paths->Store().At(0);
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(loaded.nodes[0].binding.value));
		EXPECT_EQ(ResolveNodePositions(loaded, BindingContext{}).positions[0], glm::vec3(3, 4, 5));
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].code, "scene_objects.path_binding_unresolved");
		EXPECT_EQ(warnings[0].severity, Severity::Warning);
	}

	TEST(ScenePathObjectOriginPersistenceTests, PathOriginTargetsAreRejectedOnLoad)
	{
		for (const bool self : {true, false})
		{
			RendererWindowState window;
			std::vector<StructuredError> warnings;
			std::vector<PersistedSceneObject> objects{SavedPath(self ? "path" : "other")};
			if (!self)
			{
				PersistedScenePath other = SavedPath("");
				other.persistKey = "other";
				other.nodes[0].binding = {};
				objects.emplace_back(other);
			}
			ApplyPersistedSceneObjects(window, objects, warnings);
			ASSERT_FALSE(window.paths->Store().Empty());
			EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(window.paths->Store().At(0)->nodes[0].binding.value));
			ASSERT_EQ(warnings.size(), 1u);
			EXPECT_EQ(warnings[0].code, "scene_objects.path_binding_unresolved");
		}
	}

	TEST(ScenePathObjectOriginPersistenceTests, AmbiguousTargetKeyWarnsInsteadOfChoosingAnObject)
	{
		RendererWindowState window;
		PersistedFreeLabel label;
		label.persistKey = "duplicate";
		label.text = "target";
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {SavedPath(label.persistKey), label, label}, warnings);
		ASSERT_FALSE(window.paths->Store().Empty());
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(window.paths->Store().At(0)->nodes[0].binding.value));
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].code, "scene_objects.path_binding_unresolved");
	}

	TEST(ScenePathObjectOriginPersistenceTests, NonFiniteObjectOriginOffsetWarnsAndStaysFree)
	{
		RendererWindowState window;
		PersistedFreeLabel label;
		label.persistKey = "target";
		label.text = "target";
		PersistedScenePath path = SavedPath(label.persistKey);
		path.nodes[0].binding.offset.x = std::numeric_limits<float>::quiet_NaN();
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {path, label}, warnings);
		ASSERT_FALSE(window.paths->Store().Empty());
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(window.paths->Store().At(0)->nodes[0].binding.value));
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].code, "scene_objects.path_binding_unresolved");
	}
}
