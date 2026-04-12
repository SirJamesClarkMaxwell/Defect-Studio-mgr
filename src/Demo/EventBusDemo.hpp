#pragma once

#include <memory>

namespace DefectStudio::Demo
{
	class EventBusPublisherDemo;
	class EventBusSubscriberDemo;

	class EventBusDemo
	{
	public:
		EventBusDemo();
		~EventBusDemo();

		void Render();

	private:
		std::unique_ptr<EventBusPublisherDemo> m_Publisher;
		std::unique_ptr<EventBusSubscriberDemo> m_Subscriber;
	};
} // namespace DefectStudio::Demo
