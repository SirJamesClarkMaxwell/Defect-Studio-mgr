#pragma once

#include <cstdint>
#include <string>

#include "Core/Utils/KeyCodes.hpp"

namespace DefectStudio
{
	enum class KeyModifiers : std::uint8_t
	{
		None = 0,
		Ctrl = 1u << 0u,
		Shift = 1u << 1u,
		Alt = 1u << 2u,
		Super = 1u << 3u
	};

	[[nodiscard]] constexpr KeyModifiers operator|(KeyModifiers lhs, KeyModifiers rhs) noexcept
	{
		return static_cast<KeyModifiers>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
	}

	[[nodiscard]] constexpr KeyModifiers operator&(KeyModifiers lhs, KeyModifiers rhs) noexcept
	{
		return static_cast<KeyModifiers>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
	}

	[[nodiscard]] constexpr bool HasModifier(KeyModifiers modifiers, KeyModifiers modifier) noexcept
	{
		return (modifiers & modifier) != KeyModifiers::None;
	}

	struct KeyChord
	{
		KeyCode key = KeyCode::Unknown;
		KeyModifiers modifiers = KeyModifiers::None;

		[[nodiscard]] bool operator==(const KeyChord &other) const noexcept
		{
			return key == other.key && modifiers == other.modifiers;
		}
	};

	[[nodiscard]] std::string ToString(const KeyChord &chord);
} // namespace DefectStudio
