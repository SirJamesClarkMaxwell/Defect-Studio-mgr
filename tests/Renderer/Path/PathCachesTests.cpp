#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <type_traits>

#include "Renderer/Path/PathCaches.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] PathEvaluationKey Key(std::uint64_t geometry = 1, std::uint64_t style = 1,
			std::uint64_t binding = 1, int lod = 1)
		{
			return {{geometry, style}, binding, lod};
		}
	} // namespace

	static_assert(!std::is_copy_constructible_v<PathCaches>);
	static_assert(std::is_move_constructible_v<PathCaches>);
	static_assert(std::is_move_assignable_v<PathCaches>);

	TEST(PathCachesTests, FindMissesForEveryDifferingKeyField)
	{
		PathCaches caches;
		const SceneObjectId id{1};
		caches.Store(id, Key(), {});
		EXPECT_NE(caches.Find(id, Key()), nullptr);
		EXPECT_EQ(caches.Find(SceneObjectId{9}, Key()), nullptr);
		EXPECT_EQ(caches.Find(id, Key(2)), nullptr);
		EXPECT_EQ(caches.Find(id, Key(1, 2)), nullptr);
		EXPECT_EQ(caches.Find(id, Key(1, 1, 2)), nullptr);
		EXPECT_EQ(caches.Find(id, Key(1, 1, 1, 2)), nullptr);
	}

	TEST(PathCachesTests, StoreReplacesAnIdsEntryAndOnlyTheLastKeyHits)
	{
		PathCaches caches;
		const SceneObjectId id{1};
		for (int index = 0; index < 10; ++index)
			caches.Store(id, Key(1, 1, 1, index), {});
		EXPECT_EQ(caches.Size(), 1u);
		EXPECT_EQ(caches.Find(id, Key(1, 1, 1, 8)), nullptr);
		EXPECT_NE(caches.Find(id, Key(1, 1, 1, 9)), nullptr);
	}

	TEST(PathCachesTests, StoreWithInvalidIdReturnsReadableEmptyGeometryWithoutStoring)
	{
		PathCaches caches;
		const CachedPathGeometry &stored = caches.Store(SceneObjectId{}, Key(), {});
		EXPECT_EQ(caches.Size(), 0u);
		EXPECT_TRUE(stored.evaluated.samples.empty());
		EXPECT_TRUE(stored.stroke.tubeVertices.empty());
		EXPECT_TRUE(stored.stroke.ribbonVertices.empty());
	}

	TEST(PathCachesTests, EraseRetainOnlyAndClearRemoveOnlyTheirTargets)
	{
		PathCaches caches;
		caches.Store(SceneObjectId{1}, Key(), {});
		caches.Store(SceneObjectId{2}, Key(), {});
		caches.Store(SceneObjectId{3}, Key(), {});
		EXPECT_TRUE(caches.Erase(SceneObjectId{2}));
		EXPECT_FALSE(caches.Erase(SceneObjectId{9}));
		EXPECT_NE(caches.Find(SceneObjectId{1}, Key()), nullptr);
		EXPECT_EQ(caches.Find(SceneObjectId{2}, Key()), nullptr);
		caches.RetainOnly({SceneObjectId{3}});
		EXPECT_EQ(caches.Find(SceneObjectId{1}, Key()), nullptr);
		EXPECT_NE(caches.Find(SceneObjectId{3}, Key()), nullptr);
		caches.Clear();
		EXPECT_EQ(caches.Size(), 0u);
		caches.Store(SceneObjectId{1}, Key(), {});
		caches.RetainOnly({});
		EXPECT_EQ(caches.Size(), 0u);
	}
} // namespace DefectStudio::Tests
