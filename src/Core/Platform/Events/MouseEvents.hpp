#pragma once

#include "Core/Platform/Events/PlatformEventBase.hpp"

namespace DefectStudio
{
	class MouseButtonPressedEvent final : public Event
	{
	public:
		explicit MouseButtonPressedEvent(int button) : m_Button(button)
		{
		}

		[[nodiscard]] int GetButton() const
		{
			return m_Button;
		}

		DS_EVENT_CLASS_TYPE(MouseButtonPressed)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton)

	private:
		int m_Button;
	};

	class MouseButtonReleasedEvent final : public Event
	{
	public:
		explicit MouseButtonReleasedEvent(int button) : m_Button(button)
		{
		}

		[[nodiscard]] int GetButton() const
		{
			return m_Button;
		}

		DS_EVENT_CLASS_TYPE(MouseButtonReleased)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton)

	private:
		int m_Button;
	};

	class MouseMovedEvent final : public Event
	{
	public:
		MouseMovedEvent(float x, float y) : m_X(x), m_Y(y)
		{
		}

		[[nodiscard]] float GetX() const
		{
			return m_X;
		}
		[[nodiscard]] float GetY() const
		{
			return m_Y;
		}

		DS_EVENT_CLASS_TYPE(MouseMoved)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse)

	private:
		float m_X;
		float m_Y;
	};

	class MouseScrolledEvent final : public Event
	{
	public:
		MouseScrolledEvent(float offsetX, float offsetY) : m_OffsetX(offsetX), m_OffsetY(offsetY)
		{
		}

		[[nodiscard]] float GetOffsetX() const
		{
			return m_OffsetX;
		}
		[[nodiscard]] float GetOffsetY() const
		{
			return m_OffsetY;
		}

		DS_EVENT_CLASS_TYPE(MouseScrolled)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse)

	private:
		float m_OffsetX;
		float m_OffsetY;
	};
} // namespace DefectStudio