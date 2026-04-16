#include "Core/dspch.hpp"

#include <imgui.h>

#include "Demo/EventDispatcherDemo.hpp"

namespace DefectStudio::Demo
{
	void EventDispatcherDemo::OnEvent(Event &event)
	{
		++m_TotalEventsSeen;
		// Dispatcher fits layer-local, immediate input events (type-based routing per frame).
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(std::bind(&EventDispatcherDemo::onWindowResize, this, std::placeholders::_1));
		dispatcher.Dispatch<KeyPressedEvent>(std::bind(&EventDispatcherDemo::onKeyPressed, this, std::placeholders::_1));
		dispatcher.Dispatch<MouseMovedEvent>(std::bind(&EventDispatcherDemo::onMouseMoved, this, std::placeholders::_1));
		m_LastHandled = event.handled;
	}

	void EventDispatcherDemo::Render()
	{
		ImGui::TextUnformatted("Real use-case: handles actual events routed through LayerStack.");

		ImGui::Checkbox("Consume keyboard events in this layer", &m_ConsumeKeyboardEvents);

		ImGui::Spacing();
		ImGui::Text("Total events seen: %d", m_TotalEventsSeen);
		ImGui::Text("WindowResize handled: %d", m_WindowResizeCount);
		ImGui::Text("KeyPressed handled: %d", m_KeyPressedCount);
		ImGui::Text("MouseMoved handled: %d", m_MouseMovedCount);
		ImGui::Text("Last handled: %s", m_LastHandled ? "true" : "false");

		ImGui::Spacing();
		ImGui::TextUnformatted("Flow: GLFW -> Window callback -> Application::onEvent -> LayerStack -> this dispatcher");
		ImGui::TextUnformatted("When handler returns true, propagation is stopped for lower-priority layers.");
	}

	bool EventDispatcherDemo::onWindowResize(WindowResizeEvent &event)
	{
		++m_WindowResizeCount;
		(void)event;
		return false;
	}

	bool EventDispatcherDemo::onKeyPressed(KeyPressedEvent &event)
	{
		++m_KeyPressedCount;
		(void)event;
		// Allow this layer to stop propagation for lower-priority layers when desired.
		return m_ConsumeKeyboardEvents;
	}

	bool EventDispatcherDemo::onMouseMoved(MouseMovedEvent &event)
	{
		++m_MouseMovedCount;
		(void)event;
		return false;
	}
} // namespace DefectStudio::Demo
