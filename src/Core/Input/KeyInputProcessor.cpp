#include "Core/dspch.hpp"

#include "Core/Input/KeyInputProcessor.hpp"

#include "Core/Input/ContextManager.hpp"

namespace DefectStudio
{
	KeyInputProcessor::KeyInputProcessor(KeymapResolver &resolver, ContextManager &contextManager)
		: m_Resolver(resolver), m_ContextManager(contextManager)
	{
	}

	Result<KeyInputResult> KeyInputProcessor::HandleKeyPressed(const KeyChord &chord) const
	{
		std::optional<KeyBinding> binding = m_Resolver.Resolve(chord, m_ContextManager);
		if (!binding)
			return KeyInputResult{};

		KeyInputResult inputResult;
		inputResult.handled = true;
		inputResult.commandId = binding->commandId;
		return inputResult;
	}
} // namespace DefectStudio
