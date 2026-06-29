#pragma once

#include <optional>

#include <imgui.h>

#include "Core/Input/KeyChord.hpp"

namespace DefectStudio
{
	class ImGuiInputTranslator
	{
	public:
		[[nodiscard]] static std::optional<KeyCode> TranslateKey(ImGuiKey key);
		[[nodiscard]] static std::optional<KeyChord> PollPressedChord();
	};
} // namespace DefectStudio
