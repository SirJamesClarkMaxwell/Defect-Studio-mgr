#include "Core/dspch.hpp"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Logger.hpp"
#include "Demo/DemoLayer.hpp"
#include "Demo/EventDispatcherDemo.hpp"
#include "Demo/EventBusDemo.hpp"

#include "Demo/JobSystemDemo.hpp"

namespace DefectStudio::Demo
{
	DemoLayer::DemoLayer() : Layer("DemoLayer")
	{
	}

	DemoLayer::~DemoLayer() = default;

	void DemoLayer::OnAttach()
	{
		DS_LOG_INFO("DemoLayer attached");
		m_EventDispatcherDemo = CreateUnique<EventDispatcherDemo>();
		m_DemoEventBus = CreateRef<EventBus>();
		m_EventBusDemo = CreateUnique<EventBusDemo>(m_DemoEventBus);
		m_JobSystemDemo = CreateUnique<JobSystemDemo>();
	}

	void DemoLayer::OnDetach()
	{
		DS_LOG_INFO("DemoLayer detached");
		m_JobSystemDemo.reset();
		m_EventDispatcherDemo.reset();
		m_EventBusDemo.reset();
		m_DemoEventBus.reset();
	}

	void DemoLayer::OnEvent(Event &event)
	{
		if (m_EventDispatcherDemo)
			m_EventDispatcherDemo->OnEvent(event);
	}

	void DemoLayer::OnImGuiRender()
	{
		if (m_EventDispatcherDemo)
			m_EventDispatcherDemo->Render();
		if (m_EventBusDemo)
			m_EventBusDemo->Render();
		if (m_JobSystemDemo)
			m_JobSystemDemo->Render();
	}
} // namespace DefectStudio::Demo
