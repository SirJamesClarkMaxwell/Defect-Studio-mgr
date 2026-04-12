#include "Core/dspch.hpp"

#include "Core/Logger.hpp"
#include "Demo/ComplexSystemsDemo.hpp"
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
		m_EventDispatcherDemo = std::make_unique<EventDispatcherDemo>();
		m_EventBusDemo = std::make_unique<EventBusDemo>();
		m_JobSystemDemo = std::make_unique<JobSystemDemo>();
		m_ComplexSystemsDemo = std::make_unique<ComplexSystemsDemo>();
	}

	void DemoLayer::OnDetach()
	{
		DS_LOG_INFO("DemoLayer detached");
		m_EventDispatcherDemo.reset();
		m_EventBusDemo.reset();
		m_JobSystemDemo.reset();
		m_ComplexSystemsDemo.reset();
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
		if (m_ComplexSystemsDemo)
			m_ComplexSystemsDemo->Render();
	}
} // namespace DefectStudio::Demo
