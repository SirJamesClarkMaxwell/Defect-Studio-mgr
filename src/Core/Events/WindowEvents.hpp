#pragma once

#include "Core/Events/EventBase.hpp"

namespace DefectStudio
{
	class WindowCloseEvent final : public Event
	{
	public:
		WindowCloseEvent() = default;

		DS_EVENT_CLASS_TYPE(WindowClose)
		DS_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowResizeEvent final : public Event
	{
	public:
		explicit WindowResizeEvent(int width, int height) : m_Width(width), m_Height(height)
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
} // namespace DefectStudio
