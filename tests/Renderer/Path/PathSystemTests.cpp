#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <type_traits>

#include "Renderer/Path/PathSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] ScenePath MakePath(std::uint64_t id)
		{
			ScenePath path;
			path.id = SceneObjectId{id};
			return path;
		}
		[[nodiscard]] PathEvaluationKey Key() { return {{1, 1}, 1, 1}; }
	} // namespace

	static_assert(!std::is_copy_constructible_v<PathSystem>);
	static_assert(!std::is_copy_assignable_v<PathSystem>);

	TEST(PathSystemTests, ErasePathRemovesStoreAndCacheEntryButUnknownIdChangesNeither)
	{
		PathSystem system;
		ASSERT_TRUE(system.Store().Insert(MakePath(1)));
		system.Caches().Store(SceneObjectId{1}, Key(), {});
		EXPECT_FALSE(system.ErasePath(SceneObjectId{9}));
		EXPECT_TRUE(system.Store().Contains(SceneObjectId{1}));
		EXPECT_NE(system.Caches().Find(SceneObjectId{1}, Key()), nullptr);
		EXPECT_TRUE(system.ErasePath(SceneObjectId{1}));
		EXPECT_FALSE(system.Store().Contains(SceneObjectId{1}));
		EXPECT_EQ(system.Caches().Find(SceneObjectId{1}, Key()), nullptr);
	}

	TEST(PathSystemTests, ReplaceStoreClearsCachesEvenForMatchingIds)
	{
		PathSystem system;
		ASSERT_TRUE(system.Store().Insert(MakePath(1)));
		system.Caches().Store(SceneObjectId{1}, Key(), {});
		PathStore replacement;
		ASSERT_TRUE(replacement.Insert(MakePath(1)));
		system.ReplaceStore(std::move(replacement));
		EXPECT_TRUE(system.Store().Contains(SceneObjectId{1}));
		EXPECT_EQ(system.Caches().Size(), 0u);
	}

	TEST(PathSystemTests, ClearEmptiesStoreAndCache)
	{
		PathSystem system;
		ASSERT_TRUE(system.Store().Insert(MakePath(1)));
		system.Caches().Store(SceneObjectId{1}, Key(), {});
		system.Clear();
		EXPECT_TRUE(system.Store().Empty());
		EXPECT_EQ(system.Caches().Size(), 0u);
	}

	TEST(PathSystemTests, StoreCopiesAreIndependentAndCachesAreNeverShared)
	{
		PathSystem source;
		ASSERT_TRUE(source.Store().Insert(MakePath(1)));
		PathSystem preview;
		preview.ReplaceStore(source.Store());
		ASSERT_TRUE(source.Store().MutateGeometry(SceneObjectId{1}, [](ScenePath &path) { path.name = "source"; }));
		EXPECT_TRUE(preview.Store().Find(SceneObjectId{1})->name.empty());
		source.Caches().Store(SceneObjectId{1}, Key(), {});
		EXPECT_EQ(preview.Caches().Find(SceneObjectId{1}, Key()), nullptr);
	}
} // namespace DefectStudio::Tests
