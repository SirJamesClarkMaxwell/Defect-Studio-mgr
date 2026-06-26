#include "Core/dspch.hpp"

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Demo/DemoEvents.hpp"
#include "Demo/EventBusPublisherDemo.hpp"

namespace DefectStudio::Demo
{
	EventBusPublisherDemo::EventBusPublisherDemo(Ref<EventBus> bus)
		: m_Bus(std::move(bus))
	{
	}

	void EventBusPublisherDemo::Render()
	{
		ImGui::TextUnformatted("Publishes to global EventBus from Application.");
		// EventBus is for cross-system, decoupled notifications (not tied to the input/event pipeline).
		ImGui::TextUnformatted("Immediate = Publish/PublishNew. Deferred = Queue/QueueNew + ProcessQueue.");

		if (ImGui::Button("Publish FileOpened"))
		{
			++m_PublishedFileEvents;
			FileOpenedBusEvent event{buildFileName()};
			m_Bus->Publish(event);
		}

		if (ImGui::Button("Publish DataProcessed"))
		{
			++m_PublishedDataEvents;
			m_Bus->PublishNew<DataProcessedBusEvent>(buildSummary());
		}

		if (ImGui::Button("Publish Pipeline Status"))
		{
			++m_PublishedPipelineEvents;
			PipelineStatusBusEvent event{"pipeline heartbeat #" + std::to_string(m_PublishedPipelineEvents)};
			m_Bus->Publish(event);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Deferred delivery (simulates work produced off-thread). Event is queued until ProcessQueue.");
		if (ImGui::Button("Queue FileOpened"))
		{
			++m_QueuedFileEvents;
			m_Bus->QueueNew<FileOpenedBusEvent>(buildFileName());
		}

		if (ImGui::Button("Queue Pipeline Status"))
		{
			++m_QueuedPipelineEvents;
			m_Bus->Queue(PipelineStatusBusEvent{"queued status #" + std::to_string(m_QueuedPipelineEvents)});
		}

		ImGui::Text("Queued events: %zu", m_Bus->GetQueuedEventCount());
		if (ImGui::Button("Process queued events"))
			m_Bus->ProcessQueue();

		ImGui::Separator();
		ImGui::Text("Published FileOpened: %d", m_PublishedFileEvents);
		ImGui::Text("Published DataProcessed: %d", m_PublishedDataEvents);
		ImGui::Text("Published PipelineStatus: %d", m_PublishedPipelineEvents);
		ImGui::Text("Queued FileOpened: %d", m_QueuedFileEvents);
		ImGui::Text("Queued PipelineStatus: %d", m_QueuedPipelineEvents);
	}

	std::string EventBusPublisherDemo::buildFileName() const
	{
		return "document_" + std::to_string(m_PublishedFileEvents) + ".txt";
	}

	std::string EventBusPublisherDemo::buildSummary() const
	{
		return "Result batch #" + std::to_string(m_PublishedDataEvents);
	}
} // namespace DefectStudio::Demo
