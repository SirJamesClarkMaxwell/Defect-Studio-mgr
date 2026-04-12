#include "Core/dspch.hpp"

#include <imgui.h>

#include "App/Application.hpp"
#include "Demo/DemoEvents.hpp"
#include "Demo/EventBusPublisherDemo.hpp"

namespace DefectStudio::Demo
{
	void EventBusPublisherDemo::Render()
	{
		ImGui::Begin("Demo/EventBus Publisher");
		ImGui::TextUnformatted("Publishes to global EventBus from Application.");

		if (ImGui::Button("Publish FileOpened"))
		{
			++m_PublishedFileEvents;
			Application::Get().GetEventBus().Publish(FileOpenedBusEvent{BuildFileName()});
		}

		if (ImGui::Button("Publish DataProcessed"))
		{
			++m_PublishedDataEvents;
			Application::Get().GetEventBus().Publish(DataProcessedBusEvent{BuildSummary()});
		}

		if (ImGui::Button("Publish Pipeline Status"))
		{
			++m_PublishedPipelineEvents;
			Application::Get().GetEventBus().Publish(PipelineStatusBusEvent{"pipeline heartbeat #" + std::to_string(m_PublishedPipelineEvents)});
		}

		ImGui::Separator();
		ImGui::Text("Published FileOpened: %d", m_PublishedFileEvents);
		ImGui::Text("Published DataProcessed: %d", m_PublishedDataEvents);
		ImGui::Text("Published PipelineStatus: %d", m_PublishedPipelineEvents);
		ImGui::End();
	}

	std::string EventBusPublisherDemo::BuildFileName() const
	{
		return "document_" + std::to_string(m_PublishedFileEvents) + ".txt";
	}

	std::string EventBusPublisherDemo::BuildSummary() const
	{
		return "Result batch #" + std::to_string(m_PublishedDataEvents);
	}
} // namespace DefectStudio::Demo
