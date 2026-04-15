#pragma once

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class EventBusPublisherDemo;
	class EventBusSubscriberDemo;

	class EventBusDemo
	{
	public:
		explicit EventBusDemo(Ref<EventBus> bus);
		~EventBusDemo();

		void Render();

	private:
		Ref<EventBus> m_Bus;
		Unique<EventBusPublisherDemo> m_Publisher;
		Unique<EventBusSubscriberDemo> m_Subscriber;
	};
} // namespace DefectStudio::Demo
