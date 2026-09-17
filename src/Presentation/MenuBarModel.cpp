#include "Core/dspch.hpp"
#include "Presentation/MenuBarModel.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <utility>

namespace DefectStudio
{
	const char *PanelCategoryName(PanelCategory category)
	{
		switch (category)
		{
			case PanelCategory::Scene: return "Scena";
			case PanelCategory::Structure: return "Struktura";
			case PanelCategory::Analysis: return "Analiza";
			case PanelCategory::Project: return "Projekt";
			case PanelCategory::Console: return "Konsola";
			case PanelCategory::Other: return "Inne";
		}
		return "Inne";
	}

	std::vector<PanelMenuGroup> BuildPanelMenuGroups(const std::vector<PanelMenuEntry> &panels)
	{
		constexpr std::array kCategoryOrder = {
			PanelCategory::Scene,
			PanelCategory::Structure,
			PanelCategory::Analysis,
			PanelCategory::Project,
			PanelCategory::Console,
			PanelCategory::Other};

		std::vector<PanelMenuGroup> groups;
		for (const PanelCategory category : kCategoryOrder)
		{
			PanelMenuGroup group{category};
			for (const PanelMenuEntry &panel : panels)
			{
				if (panel.category == category)
					group.titles.push_back(panel.title);
			}
			if (!group.titles.empty())
				groups.push_back(std::move(group));
		}
		return groups;
	}

	std::vector<CommandMenuGroup> BuildCommandMenuGroups(const std::vector<CommandMeta> &commands)
	{
		std::map<std::string, std::vector<CommandMenuEntry>> commandsByCategory;
		for (const CommandMeta &command : commands)
		{
			const std::string category = command.category.empty() ? "Inne" : command.category;
			commandsByCategory[category].push_back({
				command.id.value,
				command.name.empty() ? command.id.value : command.name,
				command.description,
				{}});
		}

		std::vector<CommandMenuGroup> groups;
		std::vector<CommandMenuEntry> otherCommands;
		for (auto &[category, entries] : commandsByCategory)
		{
			std::sort(entries.begin(), entries.end(), [](const CommandMenuEntry &lhs, const CommandMenuEntry &rhs) {
				return lhs.name < rhs.name;
			});

			if (category == "Inne")
				otherCommands = std::move(entries);
			else
				groups.push_back({std::move(category), std::move(entries)});
		}

		if (!otherCommands.empty())
			groups.push_back({"Inne", std::move(otherCommands)});
		return groups;
	}
} // namespace DefectStudio
