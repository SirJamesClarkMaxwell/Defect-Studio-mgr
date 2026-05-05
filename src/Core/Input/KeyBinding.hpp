#pragma once
// Convenience header – includes all Input subsystem components.

#include <optional>
#include <string_view>
#include "Core/Input/KeyChord.hpp"
#include "Core/Input/ContextExpr.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Input/KeyInputProcessor.hpp"

namespace DefectStudio
{
	[[nodiscard]] std::optional<KeyChord> ParseKeyChord(std::string_view text);
}
