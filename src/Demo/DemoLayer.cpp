#include "Core/dspch.hpp"

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Capabilities/CapabilityRegistry.hpp"
#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Core/Utils/Logger.hpp"
#include "Demo/DemoBackendRuntime.hpp"
#include "Demo/DemoCapabilities.hpp"
#include "Demo/DemoLayer.hpp"
#include "Demo/DemoNotifications.hpp"
#include "Demo/EventBusDemo.hpp"
#include "Demo/EventDispatcherDemo.hpp"
#include "Demo/JobSystemDemo.hpp"

namespace DefectStudio::Demo
{
		DemoLayer::DemoLayer(Ref<EventBus> globalEventBus, WeakRef<AssetManager> assetManager)
				: Layer("DemoLayer"),
					m_AssetManager(std::move(assetManager)),
					m_GlobalEventBus(std::move(globalEventBus))
		{
		}

	DemoLayer::~DemoLayer() = default;

	void DemoLayer::OnAttach()
	{
		DS_LOG_INFO("DemoLayer attached");

		m_EventDispatcherDemo = CreateUnique<EventDispatcherDemo>();
		m_DemoEventBus = CreateRef<EventBus>();
		m_EventBusDemo = CreateUnique<EventBusDemo>(m_DemoEventBus);
		m_JobSystemDemo = CreateUnique<JobSystemDemo>();

		m_DemoNotifier = CreateRef<Notifier>(m_DemoEventBus);
		m_NotificationsPanel = CreateUnique<DemoNotificationsPanel>(m_GlobalEventBus);

		Ref<CapabilityRegistry> capabilityRegistry = CreateRef<CapabilityRegistry>();
		capabilityRegistry->RegisterCapability(CapabilityEntry{
			"ui.notifications",
			CapabilityCategory::RuntimeDetected,
			true,
			"Demo runtime can emit notifications through its local Notifier."});

		Ref<CapabilityService> capabilityService = CreateRef<CapabilityService>(*capabilityRegistry);
		m_CapabilitiesPanel = CreateUnique<DemoCapabilitiesPanel>(capabilityRegistry, capabilityService, m_GlobalEventBus);
		m_BackendRuntime = CreateUnique<DemoBackendRuntime>(capabilityService, m_GlobalEventBus, m_AssetManager);
	}

	void DemoLayer::OnDetach()
	{
		DS_LOG_INFO("DemoLayer detached");
		m_BackendRuntime.reset();
		m_CapabilitiesPanel.reset();
		m_NotificationsPanel.reset();
		m_JobSystemDemo.reset();
		m_EventBusDemo.reset();
		m_EventDispatcherDemo.reset();
		m_DemoNotifier.reset();
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

		if (ImGui::CollapsingHeader("Capabilities", ImGuiTreeNodeFlags_DefaultOpen) && m_CapabilitiesPanel)
			m_CapabilitiesPanel->Render();

		if (ImGui::CollapsingHeader("Backend Runtime", ImGuiTreeNodeFlags_DefaultOpen) && m_BackendRuntime)
			m_BackendRuntime->Render();

		ImGui::End();

		if (m_DemoEventBus)
			m_DemoEventBus->ProcessQueue();
	}
} // namespace DefectStudio::Demo
