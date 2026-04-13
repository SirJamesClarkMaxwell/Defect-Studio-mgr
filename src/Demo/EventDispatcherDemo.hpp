#pragma once

#include "Core/Platform/Events/PlatformEvent.hpp"

namespace DefectStudio::Demo
{
	class EventDispatcherDemo
	{
	public:
		void OnEvent(Event &event);
		void Render();

	private:
		bool onWindowResize(WindowResizeEvent &event);
		bool onKeyPressed(KeyPressedEvent &event);
		bool onMouseMoved(MouseMovedEvent &event);

		int m_TotalEventsSeen = 0;
		int m_WindowResizeCount = 0;
		int m_KeyPressedCount = 0;
		int m_MouseMovedCount = 0;
		bool m_ConsumeKeyboardEvents = false;
		bool m_LastHandled = false;
	};
} // namespace DefectStudio::Demo
