#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Renderer/Path/PathStore.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] ScenePath MakePath(std::uint64_t id, std::string name = {})
		{
			ScenePath path;
			path.id = SceneObjectId{id};
			path.name = std::move(name);
			return path;
		}
	} // namespace

	TEST(PathStoreTests, InsertRejectsUnsetAndDuplicateIdsAndStartsRevisionsAtOne)
	{
		PathStore store;
		EXPECT_FALSE(store.Insert(MakePath(0)));
		EXPECT_TRUE(store.Empty());
		ASSERT_TRUE(store.Insert(MakePath(1, "first")));
		EXPECT_FALSE(store.Insert(MakePath(1, "duplicate")));
		ASSERT_EQ(store.Size(), 1u);
		EXPECT_EQ(store.Find(SceneObjectId{1})->name, "first");
		EXPECT_EQ(store.RevisionsFor(SceneObjectId{1}), (PathRevisions{1, 1}));
	}

	TEST(PathStoreTests, FindContainsSizeAndEmptyDescribeLiveEntries)
	{
		PathStore store;
		EXPECT_EQ(store.Find(SceneObjectId{9}), nullptr);
		EXPECT_FALSE(store.Contains(SceneObjectId{9}));
		ASSERT_TRUE(store.Insert(MakePath(9)));
		EXPECT_NE(store.Find(SceneObjectId{9}), nullptr);
		EXPECT_TRUE(store.Contains(SceneObjectId{9}));
		EXPECT_EQ(store.Size(), 1u);
		EXPECT_FALSE(store.Empty());
	}

	TEST(PathStoreTests, ErasePreservesTheOrderOfRemainingPaths)
	{
		PathStore store;
		ASSERT_TRUE(store.Insert(MakePath(1)));
		ASSERT_TRUE(store.Insert(MakePath(2)));
		ASSERT_TRUE(store.Insert(MakePath(3)));
		EXPECT_FALSE(store.Erase(SceneObjectId{9}));
		ASSERT_TRUE(store.Erase(SceneObjectId{2}));
		EXPECT_EQ(store.Ids(), (std::vector<SceneObjectId>{{1}, {3}}));
		ASSERT_NE(store.At(1), nullptr);
		EXPECT_EQ(store.At(1)->id, SceneObjectId{3});
	}

	TEST(PathStoreTests, MutationsOnlyBumpTheirMatchingRevisionAndDoNotRunForUnknownIds)
	{
		PathStore store;
		ASSERT_TRUE(store.Insert(MakePath(1, "first")));
		ASSERT_TRUE(store.Insert(MakePath(2, "second")));
		EXPECT_TRUE(store.MutateGeometry(SceneObjectId{1}, [](ScenePath &path) { path.name = "geometry"; }));
		EXPECT_EQ(store.RevisionsFor(SceneObjectId{1}), (PathRevisions{2, 1}));
		EXPECT_EQ(store.RevisionsFor(SceneObjectId{2}), (PathRevisions{1, 1}));
		EXPECT_TRUE(store.MutateStyle(SceneObjectId{1}, [](ScenePath &path) { path.name = "style"; }));
		EXPECT_EQ(store.RevisionsFor(SceneObjectId{1}), (PathRevisions{2, 2}));
		bool called = false;
		EXPECT_FALSE(store.MutateGeometry(SceneObjectId{9}, [&called](ScenePath &) { called = true; }));
		EXPECT_FALSE(store.MutateStyle(SceneObjectId{9}, [&called](ScenePath &) { called = true; }));
		EXPECT_FALSE(called);
	}

	TEST(PathStoreTests, UnknownRevisionsAreInvalidAndLiveRevisionsAreValid)
	{
		PathStore store;
		EXPECT_EQ(store.RevisionsFor(SceneObjectId{9}), (PathRevisions{}));
		EXPECT_FALSE(store.RevisionsFor(SceneObjectId{9}).IsValid());
		ASSERT_TRUE(store.Insert(MakePath(1)));
		EXPECT_TRUE(store.RevisionsFor(SceneObjectId{1}).IsValid());
	}

	TEST(PathStoreTests, CopyIsDeepInBothDirections)
	{
		PathStore original;
		ASSERT_TRUE(original.Insert(MakePath(1, "original")));
		PathStore copy = original;
		ASSERT_TRUE(copy.MutateStyle(SceneObjectId{1}, [](ScenePath &path) { path.name = "copy"; }));
		EXPECT_EQ(original.Find(SceneObjectId{1})->name, "original");
		EXPECT_EQ(original.RevisionsFor(SceneObjectId{1}), (PathRevisions{1, 1}));
		ASSERT_TRUE(original.MutateGeometry(SceneObjectId{1}, [](ScenePath &path) { path.name = "changed"; }));
		EXPECT_EQ(copy.Find(SceneObjectId{1})->name, "copy");
		EXPECT_EQ(copy.RevisionsFor(SceneObjectId{1}), (PathRevisions{1, 2}));
	}

	TEST(PathStoreTests, VisitUsesInsertionOrderExactlyOnce)
	{
		PathStore store;
		ASSERT_TRUE(store.Insert(MakePath(3)));
		ASSERT_TRUE(store.Insert(MakePath(1)));
		ASSERT_TRUE(store.Insert(MakePath(2)));
		std::vector<SceneObjectId> visited;
		store.Visit([&visited](const ScenePath &path) { visited.push_back(path.id); });
		EXPECT_EQ(visited, store.Ids());
	}
} // namespace DefectStudio::Tests
