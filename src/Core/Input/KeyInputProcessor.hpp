#pragma once

#include <optional>

#include "Core/Diagnostics/StructuredError.hpp"
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
		KeyInputProcessor(KeymapResolver &resolver, ContextManager &contextManager);

		[[nodiscard]] Result<KeyInputResult> HandleKeyPressed(const KeyChord &chord) const;

	private:
		KeymapResolver &m_Resolver;
		ContextManager &m_ContextManager;
	};
} // namespace DefectStudio
