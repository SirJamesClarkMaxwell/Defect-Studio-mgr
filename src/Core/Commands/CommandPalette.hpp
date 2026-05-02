#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Input/KeyBinding.hpp"

namespace DefectStudio
{
	struct CommandPaletteItem
	{
		CommandID id;
		std::string name;
		std::string category;
		std::string description;
		std::string shortcut;
		bool enabled = true;
	};

	class CommandPaletteIndex
	{
	public:
		explicit CommandPaletteIndex(CommandRegistry &commandRegistry);

		void SetKeymapResolver(const KeymapResolver *resolver, const ContextManager *contextManager = nullptr);
		void SetRecentLimit(std::size_t limit);

		[[nodiscard]] std::vector<CommandPaletteItem> Search(std::string query = {}) const;
		[[nodiscard]] std::vector<CommandID> GetRecentCommands() const;
		[[nodiscard]] Result<CommandOutcome> Execute(const CommandID &id, CommandContext context = {});

	private:
		[[nodiscard]] std::optional<std::string> findShortcut(const CommandID &id) const;
		[[nodiscard]] bool matchesQuery(const CommandMeta &meta, const std::string &query) const;
		void recordRecent(const CommandID &id);

		CommandRegistry &m_CommandRegistry;
		const KeymapResolver *m_KeymapResolver = nullptr;
		const ContextManager *m_ContextManager = nullptr;
		std::vector<CommandID> m_RecentCommands;
		std::size_t m_RecentLimit = 20;
	};
} // namespace DefectStudio
