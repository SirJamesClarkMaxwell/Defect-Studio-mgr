#pragma once

#include <string>

namespace DefectStudio::Demo
{
	class EventBusPublisherDemo
	{
	public:
		void Render();

	private:
		std::string BuildFileName() const;
		std::string BuildSummary() const;

		int m_PublishedFileEvents = 0;
		int m_PublishedDataEvents = 0;
		int m_PublishedPipelineEvents = 0;
	};
} // namespace DefectStudio::Demo
