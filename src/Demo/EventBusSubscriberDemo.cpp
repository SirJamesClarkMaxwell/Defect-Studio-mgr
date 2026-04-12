#include "Core/dspch.hpp"

#include <imgui.h>

#include "App/Application.hpp"
#include "Demo/DemoEvents.hpp"
#include "Demo/EventBusSubscriberDemo.hpp"

namespace DefectStudio::Demo
{
	EventBusSubscriberDemo::EventBusSubscriberDemo()
	{
		auto &bus = Application::Get().GetEventBus();
		m_FileOpenedSub = bus.Subscribe<FileOpenedBusEvent>(
		    std::bind(&EventBusSubscriberDemo::OnFileOpened, this, std::placeholders::_1));
		m_DataProcessedSub = bus.Subscribe<DataProcessedBusEvent>(
		    std::bind(&EventBusSubscriberDemo::OnDataProcessed, this, std::placeholders::_1));
		m_PipelineStatusSub = bus.Subscribe<PipelineStatusBusEvent>(
		    std::bind(&EventBusSubscriberDemo::OnPipelineStatus, this, std::placeholders::_1));
	}

	EventBusSubscriberDemo::~EventBusSubscriberDemo()
	{
		auto &bus = Application::Get().GetEventBus();
		bus.Unsubscribe<FileOpenedBusEvent>(m_FileOpenedSub);
		bus.Unsubscribe<DataProcessedBusEvent>(m_DataProcessedSub);
		bus.Unsubscribe<PipelineStatusBusEvent>(m_PipelineStatusSub);
	}

	void EventBusSubscriberDemo::Render()
	{
		ImGui::Begin("Demo/EventBus Subscriber");
		ImGui::TextUnformatted("Subscriber class in a separate object/window.");
		ImGui::Separator();
		ImGui::Text("Received FileOpened: %d", m_ReceivedFileEvents);
		ImGui::Text("Last filename: %s", m_LastFilename.empty() ? "<none>" : m_LastFilename.c_str());
		ImGui::Separator();
		ImGui::Text("Received DataProcessed: %d", m_ReceivedDataEvents);
		ImGui::TextWrapped("Last result: %s", m_LastResult.empty() ? "<none>" : m_LastResult.c_str());
		ImGui::Separator();
		ImGui::Text("Received PipelineStatus: %d", m_ReceivedPipelineEvents);
		ImGui::TextWrapped("Last status: %s", m_LastPipelineMessage.empty() ? "<none>" : m_LastPipelineMessage.c_str());
		ImGui::End();
	}

	void EventBusSubscriberDemo::OnFileOpened(const FileOpenedBusEvent &event)
	{
		++m_ReceivedFileEvents;
		m_LastFilename = event.filename;
	}

	void EventBusSubscriberDemo::OnDataProcessed(const DataProcessedBusEvent &event)
	{
		++m_ReceivedDataEvents;
		m_LastResult = event.resultSummary;
	}

	void EventBusSubscriberDemo::OnPipelineStatus(const PipelineStatusBusEvent &event)
	{
		++m_ReceivedPipelineEvents;
		m_LastPipelineMessage = event.message;
	}
} // namespace DefectStudio::Demo
