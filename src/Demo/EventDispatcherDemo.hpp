#pragma once

#include "Core/Event.hpp"

namespace DefectStudio::Demo
{
	class EventDispatcherDemo
	{
	public:
		void OnEvent(Event &event);
		void Render();

	private:
		bool OnWindowResize(WindowResizeEvent &event);
		bool OnKeyPressed(KeyPressedEvent &event);
		bool OnMouseMoved(MouseMovedEvent &event);

		int m_TotalEventsSeen = 0;
		int m_WindowResizeCount = 0;
		int m_KeyPressedCount = 0;
		int m_MouseMovedCount = 0;
		bool m_ConsumeKeyboardEvents = false;
		bool m_LastHandled = false;
	};
} // namespace DefectStudio::Demo
