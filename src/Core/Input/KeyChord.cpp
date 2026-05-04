#include "Core/dspch.hpp"

#include "Core/Input/KeyChord.hpp"

#include <sstream>

namespace DefectStudio
{
	[[nodiscard]] static std::string keyName(KeyCode key)
	{
		const auto native = static_cast<int>(key);
		if (key >= KeyCode::A && key <= KeyCode::Z)
			return std::string(1, static_cast<char>(native));
		if (key >= KeyCode::D0 && key <= KeyCode::D9)
			return std::string(1, static_cast<char>(native));

		switch (key)
		{
		case KeyCode::Space:
			return "Space";
		case KeyCode::Enter:
			return "Enter";
		case KeyCode::Tab:
			return "Tab";
		case KeyCode::Escape:
			return "Esc";
		case KeyCode::Delete:
			return "Delete";
		case KeyCode::Backspace:
			return "Backspace";
		case KeyCode::Left:
			return "Left";
		case KeyCode::Right:
			return "Right";
		case KeyCode::Up:
			return "Up";
		case KeyCode::Down:
			return "Down";
		case KeyCode::F1:
		case KeyCode::F2:
		case KeyCode::F3:
		case KeyCode::F4:
		case KeyCode::F5:
		case KeyCode::F6:
		case KeyCode::F7:
		case KeyCode::F8:
		case KeyCode::F9:
		case KeyCode::F10:
		case KeyCode::F11:
		case KeyCode::F12:
			return "F" + std::to_string(native - static_cast<int>(KeyCode::F1) + 1);
		default:
			return std::to_string(native);
		}
	}

	std::string ToString(const KeyChord &chord)
	{
		std::ostringstream out;
		if (HasModifier(chord.modifiers, KeyModifiers::Ctrl))
			out << "Ctrl+";
		if (HasModifier(chord.modifiers, KeyModifiers::Shift))
			out << "Shift+";
		if (HasModifier(chord.modifiers, KeyModifiers::Alt))
			out << "Alt+";
		if (HasModifier(chord.modifiers, KeyModifiers::Super))
			out << "Super+";
		out << keyName(chord.key);
		return out.str();
	}
} // namespace DefectStudio
