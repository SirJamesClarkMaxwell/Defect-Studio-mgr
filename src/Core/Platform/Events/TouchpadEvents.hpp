#pragma once

#include "Core/Platform/Events/PlatformEventBase.hpp"

namespace DefectStudio
{
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
} // namespace DefectStudio