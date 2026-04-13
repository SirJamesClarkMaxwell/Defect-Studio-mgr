#pragma once

#include "Core/Layer.hpp"
#include "Core/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class EventDispatcherDemo;
	class EventBusDemo;

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
	};
} // namespace DefectStudio::Demo
