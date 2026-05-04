#include "Core/dspch.hpp"

#include "Core/Input/KeyInputProcessor.hpp"

#include "Core/Input/ContextManager.hpp"

namespace DefectStudio
{
	KeyInputProcessor::KeyInputProcessor(CommandRegistry &commandRegistry, KeymapResolver &resolver, ContextManager &contextManager)
		: m_CommandRegistry(commandRegistry), m_Resolver(resolver), m_ContextManager(contextManager)
	{
	}

	Result<KeyInputResult> KeyInputProcessor::HandleKeyPressed(const KeyChord &chord, CommandContext context)
	{
		std::optional<KeyBinding> binding = m_Resolver.Resolve(chord, m_ContextManager);
		if (!binding)
			return KeyInputResult{};

		if (context.GetSource().empty())
			context.SetSource("KeyInputProcessor");

		auto result = m_CommandRegistry.Execute(binding->commandId, std::move(context));
		if (!result)
			return result.Error();

		KeyInputResult inputResult;
		inputResult.handled = true;
		inputResult.commandId = binding->commandId;
		return inputResult;
	}
} // namespace DefectStudio
