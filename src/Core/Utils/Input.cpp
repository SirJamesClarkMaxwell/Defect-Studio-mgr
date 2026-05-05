#include "Core/dspch.hpp"

#include "Core/Utils/Input.hpp"

namespace DefectStudio
{
	namespace
	{
		InputBackend s_Backend;
	}

	void Input::SetBackend(InputBackend backend)
	{
		s_Backend = std::move(backend);
	}

	void Input::ResetBackend()
	{
		s_Backend = {};
	}

	bool Input::IsKeyDown(KeyCode code)
	{
		if (!s_Backend.isKeyDown)
			return false;
		return s_Backend.isKeyDown(code);
	}

	bool Input::IsMouseButtonDown(MouseCode code)
	{
		if (!s_Backend.isMouseButtonDown)
			return false;
		return s_Backend.isMouseButtonDown(code);
	}

	KeyModifiers Input::GetCurrentKeyModifiers()
	{
		KeyModifiers modifiers = KeyModifiers::None;
		if (IsKeyDown(KeyCode::LeftControl) || IsKeyDown(KeyCode::RightControl))
			modifiers = modifiers | KeyModifiers::Ctrl;
		if (IsKeyDown(KeyCode::LeftShift) || IsKeyDown(KeyCode::RightShift))
			modifiers = modifiers | KeyModifiers::Shift;
		if (IsKeyDown(KeyCode::LeftAlt) || IsKeyDown(KeyCode::RightAlt))
			modifiers = modifiers | KeyModifiers::Alt;
		if (IsKeyDown(KeyCode::LeftSuper) || IsKeyDown(KeyCode::RightSuper))
			modifiers = modifiers | KeyModifiers::Super;
		return modifiers;
	}

	KeyChord Input::MakeKeyChord(KeyCode code)
	{
		return KeyChord{code, GetCurrentKeyModifiers()};
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		if (!s_Backend.mousePosition)
			return {0.0f, 0.0f};
		return s_Backend.mousePosition();
	}

	float Input::GetMouseX()
	{
		return GetMousePosition().first;
	}

	float Input::GetMouseY()
	{
		return GetMousePosition().second;
	}
} // namespace DefectStudio
