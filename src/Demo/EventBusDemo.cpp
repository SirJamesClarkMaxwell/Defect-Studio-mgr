#include "Core/dspch.hpp"

#include "Demo/EventBusDemo.hpp"
#include "Demo/EventBusPublisherDemo.hpp"
#include "Demo/EventBusSubscriberDemo.hpp"

namespace DefectStudio::Demo
{
	EventBusDemo::EventBusDemo()
	{
		m_Publisher = std::make_unique<EventBusPublisherDemo>();
		m_Subscriber = std::make_unique<EventBusSubscriberDemo>();
	}

	EventBusDemo::~EventBusDemo()
	{
		m_Publisher.reset();
		m_Subscriber.reset();
	}

	void EventBusDemo::Render()
	{
		if (m_Publisher)
			m_Publisher->Render();
		if (m_Subscriber)
			m_Subscriber->Render();
	}
} // namespace DefectStudio::Demo
