#pragma once

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class DemoBackendRuntime;
	class DemoNotificationsPanel;
	class EventDispatcherDemo;
	class EventBusDemo;
	class JobSystemDemo;

	class DemoLayer final : public Layer
	{
	public:
		DemoLayer();
		~DemoLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event &event) override;
		void OnImGuiRender() override;

	private:
		Ref<EventBus> m_DemoEventBus;
		Unique<EventDispatcherDemo> m_EventDispatcherDemo;
		Unique<EventBusDemo> m_EventBusDemo;
		Unique<JobSystemDemo> m_JobSystemDemo;
		Unique<DemoNotificationsPanel> m_NotificationsPanel;
		Unique<DemoBackendRuntime> m_BackendRuntime;
	};
} // namespace DefectStudio::Demo
