#include <gtest/gtest.h>

#include "Presentation/Panels/ViewportToolbars.hpp"

namespace DefectStudio::Tests
{
	TEST(ViewportSelectionModeEntriesTests, ListsTheFiveExistingModesInShortcutOrder)
	{
		const auto &entries = ViewportSelectionModeEntries();

		ASSERT_EQ(entries.size(), 5u);
		EXPECT_EQ(entries[0].name, "Atoms only");
		EXPECT_EQ(entries[0].shortcut, "Ctrl+1");
		EXPECT_EQ(entries[0].iconFileName, "tool-mode-atoms.png");
		EXPECT_EQ(entries[1].name, "Atoms + Bonds");
		EXPECT_EQ(entries[1].shortcut, "Ctrl+2");
		EXPECT_EQ(entries[2].name, "Bonds + Labels");
		EXPECT_EQ(entries[2].shortcut, "Ctrl+3");
		EXPECT_EQ(entries[3].name, "All");
		EXPECT_EQ(entries[3].shortcut, "Ctrl+4");
		EXPECT_EQ(entries[4].name, "Labels only");
		EXPECT_EQ(entries[4].shortcut, "Ctrl+5");
	}

	TEST(ViewportSelectionModeEntriesTests, ResolvesEveryExistingMaskToItsEntry)
	{
		EXPECT_EQ(ResolveViewportSelectionModeEntryIndex(true, false, false), 0u);
		EXPECT_EQ(ResolveViewportSelectionModeEntryIndex(true, true, false), 1u);
		EXPECT_EQ(ResolveViewportSelectionModeEntryIndex(false, true, true), 2u);
		EXPECT_EQ(ResolveViewportSelectionModeEntryIndex(true, true, true), 3u);
		EXPECT_EQ(ResolveViewportSelectionModeEntryIndex(false, false, true), 4u);
	}
} // namespace DefectStudio::Tests
