#include "Core/dspch.hpp"

#include "Core/Commands/CommandPalette.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::string toLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}
	}

	CommandPaletteIndex::CommandPaletteIndex(CommandRegistry &commandRegistry)
		: m_CommandRegistry(commandRegistry)
	{
	}

	void CommandPaletteIndex::SetKeymapResolver(const KeymapResolver *resolver, const ContextManager *contextManager)
	{
		m_KeymapResolver = resolver;
		m_ContextManager = contextManager;
	}

	void CommandPaletteIndex::SetRecentLimit(std::size_t limit)
	{
		m_RecentLimit = limit;
		if (m_RecentCommands.size() > m_RecentLimit)
			m_RecentCommands.resize(m_RecentLimit);
	}

	std::vector<CommandPaletteItem> CommandPaletteIndex::Search(std::string query) const
	{
		query = toLower(std::move(query));

		std::vector<CommandPaletteItem> items;
		std::vector<CommandMeta> commands = m_CommandRegistry.ListCommands();
		std::sort(commands.begin(), commands.end(), [](const CommandMeta &lhs, const CommandMeta &rhs) {
			if (lhs.category == rhs.category)
				return lhs.name < rhs.name;
			return lhs.category < rhs.category;
		});

		for (const CommandMeta &meta : commands)
		{
			if (HasFlag(meta.flags, CommandFlags::HiddenFromPalette) || !MatchesQuery(meta, query))
				continue;

			CommandPaletteItem item;
			item.id = meta.id;
			item.name = meta.name;
			item.category = meta.category;
			item.description = meta.description;
			item.enabled = m_CommandRegistry.CanExecute(meta.id).HasValue();
			if (auto shortcut = FindShortcut(meta.id))
				item.shortcut = *shortcut;
			items.push_back(std::move(item));
		}
		return items;
	}

	std::vector<CommandID> CommandPaletteIndex::GetRecentCommands() const
	{
		return m_RecentCommands;
	}

	Result<CommandOutcome> CommandPaletteIndex::Execute(const CommandID &id, CommandContext context)
	{
		auto result = m_CommandRegistry.Execute(id, std::move(context));
		if (result)
			RecordRecent(id);
		return result;
	}

	std::optional<std::string> CommandPaletteIndex::FindShortcut(const CommandID &id) const
	{
		if (m_KeymapResolver == nullptr)
			return std::nullopt;

		ContextManager emptyContext;
		const ContextManager &context = m_ContextManager == nullptr ? emptyContext : *m_ContextManager;
		for (const KeyBinding &binding : m_KeymapResolver->ListBindings())
		{
			if (!binding.enabled || binding.commandId.value != id.value || !binding.when.Matches(context))
				continue;
			return ToString(binding.chord);
		}
		return std::nullopt;
	}

	bool CommandPaletteIndex::MatchesQuery(const CommandMeta &meta, const std::string &query) const
	{
		if (query.empty())
			return true;

		const std::string haystack = toLower(meta.name + " " + meta.category + " " + meta.description + " " + meta.id.value);
		return haystack.find(query) != std::string::npos;
	}

	void CommandPaletteIndex::RecordRecent(const CommandID &id)
	{
		auto it = std::find_if(m_RecentCommands.begin(), m_RecentCommands.end(), [&id](const CommandID &recent) {
			return recent.value == id.value;
		});
		if (it != m_RecentCommands.end())
			m_RecentCommands.erase(it);

		m_RecentCommands.insert(m_RecentCommands.begin(), id);
		if (m_RecentCommands.size() > m_RecentLimit)
			m_RecentCommands.resize(m_RecentLimit);
	}
} // namespace DefectStudio
