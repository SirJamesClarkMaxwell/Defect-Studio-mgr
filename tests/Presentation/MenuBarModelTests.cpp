#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Presentation/MenuBarModel.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] CommandMeta MakeCommand(
			const char *id, const char *name, const char *category, const char *description = "")
		{
			CommandMeta meta;
			meta.id = CommandID{id};
			meta.name = name;
			meta.category = category;
			meta.description = description;
			return meta;
		}
	} // namespace

	// --- panels ------------------------------------------------------------------------------

	TEST(PanelMenuGroupsTests, GroupsFollowTheCategoryOrderNotTheInputOrder)
	{
		const std::vector<PanelMenuEntry> panels = {
			{"Log", PanelCategory::Console},
			{"Widok 3D", PanelCategory::Scene},
			{"Teoria grup", PanelCategory::Analysis},
			{"Outliner", PanelCategory::Scene}};

		const std::vector<PanelMenuGroup> groups = BuildPanelMenuGroups(panels);
		ASSERT_EQ(groups.size(), 3u);
		EXPECT_EQ(groups[0].category, PanelCategory::Scene);
		EXPECT_EQ(groups[1].category, PanelCategory::Analysis);
		EXPECT_EQ(groups[2].category, PanelCategory::Console);
		// Registration order is meaningful inside a group and must survive.
		EXPECT_EQ(groups[0].titles, std::vector<std::string>({"Widok 3D", "Outliner"}));
	}

	TEST(PanelMenuGroupsTests, EmptyCategoriesAreDropped)
	{
		const std::vector<PanelMenuGroup> groups =
			BuildPanelMenuGroups({{"Widok 3D", PanelCategory::Scene}});
		ASSERT_EQ(groups.size(), 1u);
		EXPECT_EQ(groups[0].category, PanelCategory::Scene);

		EXPECT_TRUE(BuildPanelMenuGroups({}).empty());
	}

	TEST(PanelMenuGroupsTests, UncategorisedPanelsGoLastButStayReachable)
	{
		const std::vector<PanelMenuGroup> groups = BuildPanelMenuGroups(
			{{"Cos nowego", PanelCategory::Other}, {"Widok 3D", PanelCategory::Scene}});
		ASSERT_EQ(groups.size(), 2u);
		EXPECT_EQ(groups[0].category, PanelCategory::Scene);
		EXPECT_EQ(groups[1].category, PanelCategory::Other);
		EXPECT_EQ(groups[1].titles, std::vector<std::string>({"Cos nowego"}));
	}

	TEST(PanelCategoryNameTests, EveryCategoryHasANonEmptyDistinctName)
	{
		constexpr PanelCategory kAll[] = {PanelCategory::Scene, PanelCategory::Structure,
			PanelCategory::Analysis, PanelCategory::Project, PanelCategory::Console,
			PanelCategory::Other};

		std::vector<std::string> seen;
		for (const PanelCategory category : kAll)
		{
			const std::string name = PanelCategoryName(category);
			EXPECT_FALSE(name.empty());
			EXPECT_EQ(std::find(seen.begin(), seen.end(), name), seen.end()) << "duplicate " << name;
			seen.push_back(name);
		}
	}

	// --- commands ----------------------------------------------------------------------------

	TEST(CommandMenuGroupsTests, CategoriesSortAlphabeticallyAndCommandsByName)
	{
		const std::vector<CommandMenuGroup> groups = BuildCommandMenuGroups({
			MakeCommand("view.reset", "Zresetuj widok", "Widok"),
			MakeCommand("edit.undo", "Cofnij", "Edycja"),
			MakeCommand("view.align", "Wyrownaj do osi", "Widok"),
			MakeCommand("edit.redo", "Ponow", "Edycja"),
		});

		ASSERT_EQ(groups.size(), 2u);
		EXPECT_EQ(groups[0].category, "Edycja");
		EXPECT_EQ(groups[1].category, "Widok");
		ASSERT_EQ(groups[0].commands.size(), 2u);
		EXPECT_EQ(groups[0].commands[0].name, "Cofnij");
		EXPECT_EQ(groups[0].commands[1].name, "Ponow");
		ASSERT_EQ(groups[1].commands.size(), 2u);
		EXPECT_EQ(groups[1].commands[0].name, "Wyrownaj do osi");
		EXPECT_EQ(groups[1].commands[1].name, "Zresetuj widok");
	}

	TEST(CommandMenuGroupsTests, UncategorisedCommandsLandInOneBucketAtTheEnd)
	{
		const std::vector<CommandMenuGroup> groups = BuildCommandMenuGroups({
			MakeCommand("misc.thing", "Cos", ""),
			MakeCommand("zzz.last", "Ostatnie", "Zzz"),
			MakeCommand("edit.undo", "Cofnij", "Edycja"),
		});

		ASSERT_EQ(groups.size(), 3u);
		EXPECT_EQ(groups[0].category, "Edycja");
		EXPECT_EQ(groups[1].category, "Zzz");
		// The empty-category bucket sorts last regardless of where its name would fall.
		EXPECT_EQ(groups[2].category, "Inne");
		ASSERT_EQ(groups[2].commands.size(), 1u);
		EXPECT_EQ(groups[2].commands[0].id, "misc.thing");
	}

	TEST(CommandMenuGroupsTests, ANamelessCommandFallsBackToItsIdRatherThanVanishing)
	{
		const std::vector<CommandMenuGroup> groups =
			BuildCommandMenuGroups({MakeCommand("renderer.atoms.add", "", "Atomy")});
		ASSERT_EQ(groups.size(), 1u);
		ASSERT_EQ(groups[0].commands.size(), 1u);
		EXPECT_EQ(groups[0].commands[0].name, "renderer.atoms.add");
		EXPECT_EQ(groups[0].commands[0].id, "renderer.atoms.add");
	}

	TEST(CommandMenuGroupsTests, DescriptionIsCarriedThroughForTheTooltip)
	{
		const std::vector<CommandMenuGroup> groups = BuildCommandMenuGroups(
			{MakeCommand("edit.undo", "Cofnij", "Edycja", "Cofa ostatnia operacje.")});
		ASSERT_EQ(groups.size(), 1u);
		ASSERT_EQ(groups[0].commands.size(), 1u);
		EXPECT_EQ(groups[0].commands[0].description, "Cofa ostatnia operacje.");
		// The keymap is consulted by the caller, not here - this model leaves the chord empty.
		EXPECT_TRUE(groups[0].commands[0].chord.empty());
	}

	TEST(CommandMenuGroupsTests, NoCommandsMeansNoMenus)
	{
		EXPECT_TRUE(BuildCommandMenuGroups({}).empty());
	}
} // namespace DefectStudio::Tests
