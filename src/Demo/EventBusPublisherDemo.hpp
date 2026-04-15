#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class EventBusPublisherDemo
	{
	public:
		explicit EventBusPublisherDemo(Ref<EventBus> bus);
		void Render();

	private:
		std::string buildFileName() const;
		std::string buildSummary() const;

		Ref<EventBus> m_Bus;
		int m_PublishedFileEvents = 0; 
		int m_PublishedDataEvents = 0; 
		int m_PublishedPipelineEvents = 0; 
		int m_QueuedFileEvents = 0; 
		int m_QueuedPipelineEvents = 0; 
	};
} // namespace DefectStudio::Demo
