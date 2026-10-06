#pragma once

#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"

namespace DefectStudio
{
	// Which drawer of the Widok menu a panel belongs in. A flat list of every registered panel had
	// grown past the point where anyone could find anything in it.
	//
	// The order of the enumerators IS the order the menu draws its submenus in, so it runs roughly
	// from "what you look at" to "what tells you what happened".
	enum class PanelCategory
	{
		Scene,
		Structure,
		Analysis,
		Project,
		Console,
		// Anything that has not declared a category. Drawn last, so a newly added panel is still
		// reachable the moment it is registered, before anyone gets round to filing it.
		Other
	};

	// Display name for a category, in the UI language of the menu bar (Polish, like the rest of
	// EditorLayer's menus).
	[[nodiscard]] const char *PanelCategoryName(PanelCategory category);

	// One panel as the menu needs it. Deliberately not an IPanel pointer - grouping is a pure
	// function so it can be tested without constructing panels or an ImGui context.
	struct PanelMenuEntry
	{
		std::string title;
		PanelCategory category = PanelCategory::Other;
	};

	struct PanelMenuGroup
	{
		PanelCategory category = PanelCategory::Other;
		// In the order the panels were registered - that order is deliberate in EditorLayer and
		// re-sorting alphabetically would scramble it.
		std::vector<std::string> titles;
	};

	// Groups panels by category, dropping empty categories, in the enumerator order above. Panels
	// keep their registration order within a group.
	[[nodiscard]] std::vector<PanelMenuGroup> BuildPanelMenuGroups(const std::vector<PanelMenuEntry> &panels);

	// One command as the menu needs it. `chord` is the keybinding to show on the right of the menu
	// item, empty when the command has none bound.
	struct CommandMenuEntry
	{
		std::string id;
		std::string name;
		std::string description;
		std::string chord;
	};

	struct CommandMenuGroup
	{
		// CommandMeta::category verbatim - these are authored at registration and there is no fixed
		// set of them, which is why this is a string and PanelCategory is an enum.
		std::string category;
		std::vector<CommandMenuEntry> commands;
	};

	// Turns the command registry's own metadata into menus: one submenu per CommandMeta::category,
	// categories sorted alphabetically with the empty-category bucket last under "Inne", commands
	// sorted by display name within each. A command whose `name` is empty falls back to its id, so
	// nothing becomes unreachable through a missing string.
	//
	// This is why the menu bar is generated rather than hand-listed: every command registered
	// anywhere in the app shows up here without a second list to keep in sync, and the keybinding
	// shown is whatever the keymap actually has bound right now.
	[[nodiscard]] std::vector<CommandMenuGroup> BuildCommandMenuGroups(const std::vector<CommandMeta> &commands);
} // namespace DefectStudio
