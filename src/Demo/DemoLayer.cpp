#include "Core/dspch.hpp"

#include <imgui.h>

#include "App/Application.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Logger.hpp"
#include "Demo/DemoBackendRuntime.hpp"
#include "Demo/DemoLayer.hpp"
#include "Demo/DemoNotifications.hpp"
#include "Demo/EventBusDemo.hpp"
#include "Demo/EventDispatcherDemo.hpp"
#include "Demo/JobSystemDemo.hpp"

namespace DefectStudio::Demo
{
	DemoLayer::DemoLayer() : Layer("DemoLayer")
	{
	}

	DemoLayer::~DemoLayer() = default;

	void DemoLayer::OnAttach()
	{
		DS_LOG_INFO("DemoLayer attached");

		// TODO(Sesja 6): inject via BindRuntimeServices
		auto &application = Application::Get();

		m_EventDispatcherDemo = CreateUnique<EventDispatcherDemo>();
		m_DemoEventBus = CreateRef<EventBus>();
		m_EventBusDemo = CreateUnique<EventBusDemo>(m_DemoEventBus);
		m_JobSystemDemo = CreateUnique<JobSystemDemo>();
		m_NotificationsPanel = CreateUnique<DemoNotificationsPanel>(application.GetNotifierRef());
		m_BackendRuntime = CreateUnique<DemoBackendRuntime>(&application.GetCapabilityService());
	}

	void DemoLayer::OnDetach()
	{
		DS_LOG_INFO("DemoLayer detached");
		m_BackendRuntime.reset();
		m_NotificationsPanel.reset();
		m_JobSystemDemo.reset();
		m_EventBusDemo.reset();
		m_EventDispatcherDemo.reset();
		m_DemoEventBus.reset();
	}

	void DemoLayer::OnEvent(Event &event)
	{
		if (m_EventDispatcherDemo)
			m_EventDispatcherDemo->OnEvent(event);

		if (m_BackendRuntime)
			m_BackendRuntime->OnEvent(event);
	}

	void DemoLayer::OnImGuiRender()
	{
		if (!ImGui::Begin("Demos"))
		{
			ImGui::End();
			return;
		}

		if (ImGui::CollapsingHeader("Event Dispatcher", ImGuiTreeNodeFlags_DefaultOpen) && m_EventDispatcherDemo)
			m_EventDispatcherDemo->Render();

		if (ImGui::CollapsingHeader("Event Bus", ImGuiTreeNodeFlags_DefaultOpen) && m_EventBusDemo)
			m_EventBusDemo->Render();

		if (ImGui::CollapsingHeader("Job System", ImGuiTreeNodeFlags_DefaultOpen) && m_JobSystemDemo)
			m_JobSystemDemo->Render();

		if (ImGui::CollapsingHeader("Notifications", ImGuiTreeNodeFlags_DefaultOpen) && m_NotificationsPanel)
			m_NotificationsPanel->Render();

		if (ImGui::CollapsingHeader("Backend Runtime", ImGuiTreeNodeFlags_DefaultOpen) && m_BackendRuntime)
			m_BackendRuntime->Render();

		ImGui::End();

		if (m_NotificationsPanel)
			m_NotificationsPanel->RenderToasts();
	}
} // namespace DefectStudio::Demo
