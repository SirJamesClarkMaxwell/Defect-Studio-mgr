#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "Core/Input/KeyBinding.hpp"

namespace DefectStudio
{
	static std::string trimCopy(std::string_view value)
	{
		const std::size_t first = value.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos)
			return {};
		const std::size_t last = value.find_last_not_of(" \t\r\n");
		return std::string(value.substr(first, last - first + 1));
	}

	static std::string toLowerCopy(std::string_view value)
	{
		std::string result(value);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return result;
	}

	static std::optional<KeyCode> parseKeyCode(std::string_view token)
	{
		if (token.size() == 1)
		{
			const unsigned char ch = static_cast<unsigned char>(token[0]);
			if (std::isalpha(ch))
			{
				const char upper = static_cast<char>(std::toupper(ch));
				return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (upper - 'A'));
			}
			if (std::isdigit(ch))
				return static_cast<KeyCode>(static_cast<int>(KeyCode::D0) + (ch - '0'));
		}

		const std::string lower = toLowerCopy(token);
		if (lower == "space")
			return KeyCode::Space;
		if (lower == "enter")
			return KeyCode::Enter;
		if (lower == "tab")
			return KeyCode::Tab;
		if (lower == "esc" || lower == "escape")
			return KeyCode::Escape;
		if (lower == "delete")
			return KeyCode::Delete;
		if (lower == "backspace")
			return KeyCode::Backspace;
		if (lower == "left")
			return KeyCode::Left;
		if (lower == "right")
			return KeyCode::Right;
		if (lower == "up")
			return KeyCode::Up;
		if (lower == "down")
			return KeyCode::Down;
		if (lower == "pageup")
			return KeyCode::PageUp;
		if (lower == "pagedown")
			return KeyCode::PageDown;
		if (lower == "home")
			return KeyCode::Home;
		if (lower == "end")
			return KeyCode::End;
		if (lower == "insert")
			return KeyCode::Insert;
		if (lower == "pause")
			return KeyCode::Pause;
		if (lower == "printscreen")
			return KeyCode::PrintScreen;

		if (!lower.empty() && lower[0] == 'f')
		{
			const std::string numberPart = lower.substr(1);
			if (!numberPart.empty() && std::all_of(numberPart.begin(), numberPart.end(), [](unsigned char ch) {
					return std::isdigit(ch) != 0;
				}))
			{
				const int fn = std::stoi(numberPart);
				if (fn >= 1 && fn <= 12)
					return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (fn - 1));
			}
		}

		return std::nullopt;
	}

	std::optional<KeyChord> ParseKeyChord(std::string_view text)
	{
		std::string input = trimCopy(text);
		if (input.empty())
			return std::nullopt;

		KeyModifiers modifiers = KeyModifiers::None;
		KeyCode key = KeyCode::Unknown;
		std::size_t start = 0;
		while (start <= input.size())
		{
			const std::size_t end = input.find('+', start);
			const std::string token = trimCopy(std::string_view(input).substr(start, end == std::string::npos ? input.size() - start : end - start));
			if (!token.empty())
			{
				const std::string lower = toLowerCopy(token);
				if (lower == "ctrl" || lower == "control")
					modifiers = modifiers | KeyModifiers::Ctrl;
				else if (lower == "shift")
					modifiers = modifiers | KeyModifiers::Shift;
				else if (lower == "alt")
					modifiers = modifiers | KeyModifiers::Alt;
				else if (lower == "super" || lower == "cmd" || lower == "command" || lower == "meta")
					modifiers = modifiers | KeyModifiers::Super;
				else
				{
					auto parsedKey = parseKeyCode(token);
					if (!parsedKey)
						return std::nullopt;
					if (key != KeyCode::Unknown)
						return std::nullopt;
					key = *parsedKey;
				}
			}

		if (end == std::string::npos)
			break;
			start = end + 1;
		}

		if (key == KeyCode::Unknown)
			return std::nullopt;

		return KeyChord{key, modifiers};
	}
} // namespace DefectStudio
