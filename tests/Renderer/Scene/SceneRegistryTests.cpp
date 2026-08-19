#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "Renderer/Scene/SceneRegistry.hpp"

namespace DefectStudio::Tests
{
	struct DummyComponent
	{
		int value = 0;
	};

	TEST(SceneRegistryTests, CreateEntityAddGetRemoveComponentRoundTrips)
	{
		SceneRegistry scene;
		Entity entity = scene.CreateEntity();
		ASSERT_TRUE(entity);

		entity.AddComponent<DummyComponent>(DummyComponent{42});
		ASSERT_TRUE(entity.HasComponent<DummyComponent>());
		EXPECT_EQ(entity.GetComponent<DummyComponent>().value, 42);

		entity.RemoveComponent<DummyComponent>();
		EXPECT_FALSE(entity.HasComponent<DummyComponent>());
	}

	TEST(SceneRegistryTests, DestroyEntityInvalidatesHandle)
	{
		SceneRegistry scene;
		Entity entity = scene.CreateEntity();
		const entt::entity handle = static_cast<entt::entity>(entity);
		ASSERT_TRUE(scene.Registry().valid(handle));

		scene.DestroyEntity(entity);
		EXPECT_FALSE(scene.Registry().valid(handle));
	}

	TEST(SceneRegistryTests, MultipleEntitiesIndependentComponentsViaNativeView)
	{
		SceneRegistry scene;
		Entity first = scene.CreateEntity();
		Entity second = scene.CreateEntity();
		first.AddComponent<DummyComponent>(DummyComponent{1});
		second.AddComponent<DummyComponent>(DummyComponent{2});

		std::vector<int> seenValues;
		for (const entt::entity handle : scene.Registry().view<DummyComponent>())
			seenValues.push_back(scene.Registry().get<DummyComponent>(handle).value);

		ASSERT_EQ(seenValues.size(), 2u);
		EXPECT_NE(std::find(seenValues.begin(), seenValues.end(), 1), seenValues.end());
		EXPECT_NE(std::find(seenValues.begin(), seenValues.end(), 2), seenValues.end());
	}
} // namespace DefectStudio::Tests
