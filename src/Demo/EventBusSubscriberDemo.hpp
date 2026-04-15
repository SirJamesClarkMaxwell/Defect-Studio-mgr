#pragma once

#include <string>

#include "Core/EventSystem/BusEventSystem/SubscriptionHandle.hpp"
#include "Core/Utils/Memory.hpp"
#include "Demo/DemoEvents.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class EventBusSubscriberDemo
	{
	public:
		explicit EventBusSubscriberDemo(Ref<EventBus> bus);
		~EventBusSubscriberDemo();

		void Render();

	private:
		void onFileOpened(const FileOpenedBusEvent &event);
		void onDataProcessed(const DataProcessedBusEvent &event);
		void onPipelineStatus(const PipelineStatusBusEvent &event);

		Ref<EventBus> m_Bus;
		SubscriptionHandle m_FileOpenedSub;
		SubscriptionHandle m_DataProcessedSub;
		SubscriptionHandle m_PipelineStatusSub;

		int m_ReceivedFileEvents = 0;
		int m_ReceivedDataEvents = 0;
		int m_ReceivedPipelineEvents = 0;
		std::string m_LastFilename;
		std::string m_LastResult;
		std::string m_LastPipelineMessage;
	};
} // namespace DefectStudio::Demo
