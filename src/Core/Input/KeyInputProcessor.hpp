#pragma once

#include <optional>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Input/KeymapResolver.hpp"

namespace DefectStudio
{
	class ContextManager;

	struct KeyInputResult
	{
		bool handled = false;
		std::optional<CommandID> commandId;
	};

	class KeyInputProcessor
	{
	public:
		KeyInputProcessor(CommandRegistry &commandRegistry, KeymapResolver &resolver, ContextManager &contextManager);

		[[nodiscard]] Result<KeyInputResult> HandleKeyPressed(const KeyChord &chord, CommandContext context = {});

	private:
		CommandRegistry &m_CommandRegistry;
		KeymapResolver &m_Resolver;
		ContextManager &m_ContextManager;
	};
} // namespace DefectStudio
