#pragma once

#include "Core/dspch.hpp"

namespace DefectStudio
{
	enum class EventType
	{
		None = 0,
		WindowClose,
		WindowResize,
		KeyPressed,
		KeyRepeated,
		KeyReleased,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled,
		TouchpadGesture
	};

	enum EventCategory : std::uint32_t
	{
		EventCategoryNone = 0,
		EventCategoryApplication = 1u << 0,
		EventCategoryInput = 1u << 1,
		EventCategoryKeyboard = 1u << 2,
		EventCategoryMouse = 1u << 3,
		EventCategoryMouseButton = 1u << 4,
		EventCategoryTouchpad = 1u << 5
	};

	class Event
	{
	public:
		virtual ~Event() = default;

		[[nodiscard]] virtual EventType GetEventType() const = 0;
		[[nodiscard]] virtual std::string_view GetName() const = 0;
		[[nodiscard]] virtual std::uint32_t GetCategoryFlags() const = 0;

		[[nodiscard]] bool IsInCategory(EventCategory category) const
		{
			return (GetCategoryFlags() & static_cast<std::uint32_t>(category)) != 0;
		}

		bool handled = false;
	};

	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event &event) : m_Event(event)
		{
		}

		template <typename TEvent, typename TFunction> bool Dispatch(TFunction &&function)
		{
			if (m_Event.GetEventType() != TEvent::GetStaticType())
				return false;

			m_Event.handled = std::invoke(std::forward<TFunction>(function), static_cast<TEvent &>(m_Event));
			return true;
		}

	private:
		Event &m_Event;
	};

#define DS_EVENT_CLASS_TYPE(type)                                                                                      \
	static EventType GetStaticType()                                                                                   \
	{                                                                                                                  \
		return EventType::type;                                                                                        \
	}                                                                                                                  \
	EventType GetEventType() const override                                                                            \
	{                                                                                                                  \
		return GetStaticType();                                                                                        \
	}                                                                                                                  \
	std::string_view GetName() const override                                                                          \
	{                                                                                                                  \
		return #type;                                                                                                  \
	}

#define DS_EVENT_CLASS_CATEGORY(categoryFlags)                                                                         \
	std::uint32_t GetCategoryFlags() const override                                                                    \
	{                                                                                                                  \
		return categoryFlags;                                                                                          \
	}

	class WindowCloseEvent final : public Event
	{
	public:
		DS_EVENT_CLASS_TYPE(WindowClose)
		DS_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowResizeEvent final : public Event
	{
	public:
		WindowResizeEvent(int width, int height) : m_Width(width), m_Height(height)
		{
		}

		[[nodiscard]] int GetWidth() const
		{
			return m_Width;
		}
		[[nodiscard]] int GetHeight() const
		{
			return m_Height;
		}

		DS_EVENT_CLASS_TYPE(WindowResize)
		DS_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int m_Width;
		int m_Height;
	};

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

	class TouchpadGestureEvent final : public Event
	{
	public:
		TouchpadGestureEvent(float deltaX, float deltaY) : m_DeltaX(deltaX), m_DeltaY(deltaY)
		{
		}

		[[nodiscard]] float GetDeltaX() const
		{
			return m_DeltaX;
		}
		[[nodiscard]] float GetDeltaY() const
		{
			return m_DeltaY;
		}

		DS_EVENT_CLASS_TYPE(TouchpadGesture)
		DS_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryTouchpad)

	private:
		float m_DeltaX;
		float m_DeltaY;
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
