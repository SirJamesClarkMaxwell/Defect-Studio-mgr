#pragma once

#include <string>

#include "Core/EventBus.hpp"
#include "Demo/DemoEvents.hpp"

namespace DefectStudio::Demo
{
	class EventBusSubscriberDemo
	{
	public:
		EventBusSubscriberDemo();
		~EventBusSubscriberDemo();

		void Render();

	private:
		void OnFileOpened(const FileOpenedBusEvent &event);
		void OnDataProcessed(const DataProcessedBusEvent &event);
		void OnPipelineStatus(const PipelineStatusBusEvent &event);

		EventBus::SubscriptionId m_FileOpenedSub = 0;
		EventBus::SubscriptionId m_DataProcessedSub = 0;
		EventBus::SubscriptionId m_PipelineStatusSub = 0;

		int m_ReceivedFileEvents = 0;
		int m_ReceivedDataEvents = 0;
		int m_ReceivedPipelineEvents = 0;
		std::string m_LastFilename;
		std::string m_LastResult;
		std::string m_LastPipelineMessage;
	};
} // namespace DefectStudio::Demo
