#include "Core/dspch.hpp"

#include <exception>
#include <imgui.h>
#include <string>
#include <utility>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Demo/DemoCapabilities.hpp"
#include "Events/NotificationEvents.hpp"

namespace DefectStudio::Demo
{
	DemoCapabilitiesPanel::DemoCapabilitiesPanel(
		Ref<CapabilityService> capabilityService,
		Ref<EventBus> eventBus)
		: m_CapabilityService(std::move(capabilityService)),
		  m_EventBus(std::move(eventBus))
	{
	}

	void DemoCapabilitiesPanel::Render()
	{
		renderRegistrySnapshot();
		ImGui::Spacing();
		renderRegistrationDemo();
	}

	void DemoCapabilitiesPanel::renderRegistrySnapshot() const
	{
		if (!m_CapabilityService)
		{
			ImGui::TextUnformatted("Capability demo is not initialized.");
			return;
		}

		const auto capabilities = m_CapabilityService->GetAll();

		ImGui::TextUnformatted("Local demo runtime capabilities");
		ImGui::Text("Registry locked: %s", m_CapabilityService->IsLocked() ? "yes" : "no");
		ImGui::Text("Registered capabilities: %zu", capabilities.size());
		ImGui::Text("Capability service reports 'ui.notifications': %s",
		            m_CapabilityService->IsAvailable("ui.notifications") ? "yes" : "no");

		if (ImGui::BeginTable("##application_capabilities", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Capability");
			ImGui::TableSetupColumn("Category");
			ImGui::TableSetupColumn("Available");
			ImGui::TableSetupColumn("Description");
			ImGui::TableHeadersRow();

			for (const CapabilityEntry &capability : capabilities)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(capability.name.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(categoryName(capability.category));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(capability.available ? "yes" : "no");
				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", capability.description.c_str());
			}

			ImGui::EndTable();
		}
	}

	void DemoCapabilitiesPanel::renderRegistrationDemo()
	{
		ImGui::SeparatorText("Local registration example");
		ImGui::TextWrapped("%s", m_LastCapabilityDemoAction.c_str());

		ImGui::BeginDisabled(!m_CapabilityService || m_CapabilityService->IsLocked());
		if (ImGui::Button("Register available demo capability"))
		{
			registerCapability(
				"demo.local.available",
				true,
				"Registered from DemoCapabilitiesPanel before LockAfterStartup.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Register disabled demo capability"))
		{
			registerCapability(
				"demo.local.disabled",
				false,
				"Registered from DemoCapabilitiesPanel as unavailable.");
		}
		ImGui::EndDisabled();

		if (ImGui::Button("Lock local demo registry"))
		{
			if (m_CapabilityService)
				m_CapabilityService->LockAfterStartup();
			m_LastCapabilityDemoAction = "Local demo registry locked.";
			notifyInfo("Capability registry locked", "Local demo capability registry is now locked.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Try late registration"))
		{
			registerCapability(
				"demo.local.late",
				true,
				"This registration is ignored after local registry lock.");
		}

		if (ImGui::Button("Require demo.local.available"))
			requireCapability("demo.local.available");
		ImGui::SameLine();
		if (ImGui::Button("Require demo.local.disabled"))
			requireCapability("demo.local.disabled");
		ImGui::SameLine();
		if (ImGui::Button("Require demo.local.missing"))
			requireCapability("demo.local.missing");
	}

	void DemoCapabilitiesPanel::registerCapability(std::string name, bool available, std::string description)
	{
		if (!m_CapabilityService)
			return;

		const bool locked = m_CapabilityService->IsLocked();
		m_CapabilityService->RegisterCapability(CapabilityEntry{
			name,
			CapabilityCategory::RuntimeDetected,
			available,
			std::move(description)});

		if (locked)
		{
			m_LastCapabilityDemoAction = "Registration ignored after lock: " + name;
			notifyError("Capability registration ignored", name + " was submitted after LockAfterStartup.");
			return;
		}

		m_LastCapabilityDemoAction = "Registered local capability: " + name;
		notifyInfo("Capability registered", name + " registered in the local demo registry.");
	}

	void DemoCapabilitiesPanel::requireCapability(const std::string &name)
	{
		if (!m_CapabilityService)
			return;

		Result<void> result = m_CapabilityService->Require(name);
		if (result)
		{
			m_LastCapabilityDemoAction = "Requirement satisfied: " + name;
			notifyInfo("Local capability available", name + " requirement satisfied.");
			return;
		}

		const StructuredError &error = result.Error();
		m_LastCapabilityDemoAction = error.technicalDetails;
		notifyError("Local capability missing", error.userMessage);
	}

	void DemoCapabilitiesPanel::notifyInfo(std::string title, std::string message)
	{
		if (m_EventBus)
		{
			m_EventBus->Queue(NotificationRequestedEvent{Notification{
				NotificationSeverity::Info,
				NotificationCategory::Capability,
				std::move(title),
				std::move(message),
				"DemoCapabilitiesPanel",
				Time::Now(),
				4000,
				false}});
		}
	}

	void DemoCapabilitiesPanel::notifyError(std::string title, std::string message)
	{
		if (m_EventBus)
		{
			m_EventBus->Queue(NotificationRequestedEvent{Notification{
				NotificationSeverity::Error,
				NotificationCategory::Capability,
				std::move(title),
				std::move(message),
				"DemoCapabilitiesPanel",
				Time::Now(),
				12000,
				false}});
		}
	}

	const char *DemoCapabilitiesPanel::categoryName(CapabilityCategory category)
	{
		switch (category)
		{
		case CapabilityCategory::BuildTime:
			return "BuildTime";
		case CapabilityCategory::RuntimeDetected:
			return "RuntimeDetected";
		case CapabilityCategory::Policy:
			return "Policy";
		}

		return "Unknown";
	}
} // namespace DefectStudio::Demo
