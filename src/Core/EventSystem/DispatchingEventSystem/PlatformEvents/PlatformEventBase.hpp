#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "Core/EventSystem/Common/EventControl.hpp"
#include "Core/EventSystem/Common/EventSystemCommon.hpp"

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

	class Event : public EventControl
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

		[[nodiscard]] static constexpr EventSystemKind GetSystemKind()
		{
			return EventSystemKind::Dispatching;
		}
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

} // namespace DefectStudio