#include "Core/dspch.hpp"

#include "Presentation/ImGuiInputTranslator.hpp"

#include <array>

#include <imgui_internal.h>

namespace DefectStudio
{

    [[nodiscard]] KeyModifiers CurrentImGuiModifiers()
    {
        ImGuiIO &io = ImGui::GetIO();
        KeyModifiers modifiers = KeyModifiers::None;
        if (io.KeyCtrl)
            modifiers = modifiers | KeyModifiers::Ctrl;
        if (io.KeyShift)
            modifiers = modifiers | KeyModifiers::Shift;
        if (io.KeyAlt)
            modifiers = modifiers | KeyModifiers::Alt;
        if (io.KeySuper)
            modifiers = modifiers | KeyModifiers::Super;
        return modifiers;
    }


	std::optional<KeyCode> ImGuiInputTranslator::TranslateKey(ImGuiKey key)
	{
		if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
			return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (static_cast<int>(key) - static_cast<int>(ImGuiKey_A)));
		if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
			return static_cast<KeyCode>(static_cast<int>(KeyCode::D0) + (static_cast<int>(key) - static_cast<int>(ImGuiKey_0)));

		switch (key)
		{
		case ImGuiKey_Space:
			return KeyCode::Space;
		case ImGuiKey_Comma:
			return KeyCode::Comma;
		case ImGuiKey_Minus:
			return KeyCode::Minus;
		case ImGuiKey_Equal:
			return KeyCode::Equal;
		case ImGuiKey_Period:
			return KeyCode::Period;
		case ImGuiKey_LeftArrow:
			return KeyCode::Left;
		case ImGuiKey_RightArrow:
			return KeyCode::Right;
		case ImGuiKey_UpArrow:
			return KeyCode::Up;
		case ImGuiKey_DownArrow:
			return KeyCode::Down;
		case ImGuiKey_Escape:
			return KeyCode::Escape;
		case ImGuiKey_Enter:
		case ImGuiKey_KeypadEnter:
			return KeyCode::Enter;
		case ImGuiKey_Tab:
			return KeyCode::Tab;
		case ImGuiKey_Backspace:
			return KeyCode::Backspace;
		case ImGuiKey_Delete:
			return KeyCode::Delete;
		case ImGuiKey_Insert:
			return KeyCode::Insert;
		case ImGuiKey_Home:
			return KeyCode::Home;
		case ImGuiKey_End:
			return KeyCode::End;
		case ImGuiKey_PageUp:
			return KeyCode::PageUp;
		case ImGuiKey_PageDown:
			return KeyCode::PageDown;
		default:
			break;
		}

		if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12)
			return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (static_cast<int>(key) - static_cast<int>(ImGuiKey_F1)));
		return std::nullopt;
	}

	std::optional<KeyChord> ImGuiInputTranslator::PollPressedChord()
	{
		ImGuiContext *context = ImGui::GetCurrentContext();
		if (context != nullptr)
		{
			for (int index = context->InputEventsQueue.Size - 1; index >= 0; --index)
			{
				const ImGuiInputEvent &event = context->InputEventsQueue[index];
				if (event.Type != ImGuiInputEventType_Key || !event.Key.Down)
					continue;

				const std::optional<KeyCode> translated = TranslateKey(event.Key.Key);
				if (!translated)
					continue;

				return KeyChord{*translated, CurrentImGuiModifiers()};
			}
		}

		constexpr std::array<ImGuiKey, 68> keys = {
			ImGuiKey_A,
			ImGuiKey_B,
			ImGuiKey_C,
			ImGuiKey_D,
			ImGuiKey_E,
			ImGuiKey_F,
			ImGuiKey_G,
			ImGuiKey_H,
			ImGuiKey_I,
			ImGuiKey_J,
			ImGuiKey_K,
			ImGuiKey_L,
			ImGuiKey_M,
			ImGuiKey_N,
			ImGuiKey_O,
			ImGuiKey_P,
			ImGuiKey_Q,
			ImGuiKey_R,
			ImGuiKey_S,
			ImGuiKey_T,
			ImGuiKey_U,
			ImGuiKey_V,
			ImGuiKey_W,
			ImGuiKey_X,
			ImGuiKey_Y,
			ImGuiKey_Z,
			ImGuiKey_0,
			ImGuiKey_1,
			ImGuiKey_2,
			ImGuiKey_3,
			ImGuiKey_4,
			ImGuiKey_5,
			ImGuiKey_6,
			ImGuiKey_7,
			ImGuiKey_8,
			ImGuiKey_9,
			ImGuiKey_Space,
			ImGuiKey_Comma,
			ImGuiKey_Minus,
			ImGuiKey_Equal,
			ImGuiKey_Period,
			ImGuiKey_LeftArrow,
			ImGuiKey_RightArrow,
			ImGuiKey_UpArrow,
			ImGuiKey_DownArrow,
			ImGuiKey_Escape,
			ImGuiKey_Enter,
			ImGuiKey_KeypadEnter,
			ImGuiKey_Tab,
			ImGuiKey_Backspace,
			ImGuiKey_Delete,
			ImGuiKey_Insert,
			ImGuiKey_Home,
			ImGuiKey_End,
			ImGuiKey_PageUp,
			ImGuiKey_PageDown,
			ImGuiKey_F1,
			ImGuiKey_F2,
			ImGuiKey_F3,
			ImGuiKey_F4,
			ImGuiKey_F5,
			ImGuiKey_F6,
			ImGuiKey_F7,
			ImGuiKey_F8,
			ImGuiKey_F9,
			ImGuiKey_F10,
			ImGuiKey_F11,
			ImGuiKey_F12};

		for (const ImGuiKey key : keys)
		{
			if (!ImGui::IsKeyPressed(key, false))
				continue;

			const std::optional<KeyCode> translated = TranslateKey(key);
			if (!translated)
				continue;

			return KeyChord{*translated, CurrentImGuiModifiers()};
		}

		return std::nullopt;
	}
} // namespace DefectStudio
