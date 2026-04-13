#pragma once

#include "Core/Platform/Events/PlatformEventBase.hpp"

namespace DefectStudio
{
	class KeyPressedEvent final : public Event
	{
	public:
		explicit KeyPressedEvent(int keyCode) : m_KeyCode(keyCode)
		{
		}

		[[nodiscard]] int GetKeyCode() const
		{
			return m_KeyCode;
		}

		DS_EVENT_CLASS_TYPE(KeyPressed)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)

	private:
		int m_KeyCode;
	};

	class KeyReleasedEvent final : public Event
	{
	public:
		explicit KeyReleasedEvent(int keyCode) : m_KeyCode(keyCode)
		{
		}

		[[nodiscard]] int GetKeyCode() const
		{
			return m_KeyCode;
		}

		DS_EVENT_CLASS_TYPE(KeyReleased)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)

	private:
		int m_KeyCode;
	};

	class KeyRepeatedEvent final : public Event
	{
	public:
		explicit KeyRepeatedEvent(int keyCode) : m_KeyCode(keyCode)
		{
		}

		[[nodiscard]] int GetKeyCode() const
		{
			return m_KeyCode;
		}

		DS_EVENT_CLASS_TYPE(KeyRepeated)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)

	private:
		int m_KeyCode;
	};
} // namespace DefectStudio