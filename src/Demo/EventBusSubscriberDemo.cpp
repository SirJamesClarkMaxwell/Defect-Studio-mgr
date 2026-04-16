#include "Core/dspch.hpp"

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Demo/DemoEvents.hpp"
#include "Demo/EventBusSubscriberDemo.hpp"

namespace DefectStudio::Demo
{
	EventBusSubscriberDemo::EventBusSubscriberDemo(Ref<EventBus> bus)
		: m_Bus(std::move(bus))
	{
		m_FileOpenedSub = m_Bus->Subscribe<FileOpenedBusEvent>(
			[this](const FileOpenedBusEvent &event) { onFileOpened(event); });
		m_DataProcessedSub = m_Bus->Subscribe<DataProcessedBusEvent>(
			[this](const DataProcessedBusEvent &event) { onDataProcessed(event); });
		m_PipelineStatusSub = m_Bus->Subscribe<PipelineStatusBusEvent>(
			[this](const PipelineStatusBusEvent &event) { onPipelineStatus(event); });
	}

	EventBusSubscriberDemo::~EventBusSubscriberDemo()
	{
		m_FileOpenedSub.Reset();
		m_DataProcessedSub.Reset();
		m_PipelineStatusSub.Reset();
	}

	void EventBusSubscriberDemo::Render()
	{
		ImGui::TextUnformatted("Subscriber class in a separate object/window.");
		// Separate object demonstrates lifetime ownership; subscriptions live with the subscriber.
		ImGui::Separator();
		ImGui::Text("Received FileOpened: %d", m_ReceivedFileEvents);
		ImGui::Text("Last filename: %s", m_LastFilename.empty() ? "<none>" : m_LastFilename.c_str());
		ImGui::Separator();
		ImGui::Text("Received DataProcessed: %d", m_ReceivedDataEvents);
		ImGui::TextWrapped("Last result: %s", m_LastResult.empty() ? "<none>" : m_LastResult.c_str());
		ImGui::Separator();
		ImGui::Text("Received PipelineStatus: %d", m_ReceivedPipelineEvents);
		ImGui::TextWrapped("Last status: %s", m_LastPipelineMessage.empty() ? "<none>" : m_LastPipelineMessage.c_str());
	}

	void EventBusSubscriberDemo::onFileOpened(const FileOpenedBusEvent &event)
	{
		++m_ReceivedFileEvents;
		m_LastFilename = event.filename;
	}

	void EventBusSubscriberDemo::onDataProcessed(const DataProcessedBusEvent &event)
	{
		++m_ReceivedDataEvents;
		m_LastResult = event.resultSummary;
	}

	void EventBusSubscriberDemo::onPipelineStatus(const PipelineStatusBusEvent &event)
	{
		++m_ReceivedPipelineEvents;
		m_LastPipelineMessage = event.message;
	}
} // namespace DefectStudio::Demo
