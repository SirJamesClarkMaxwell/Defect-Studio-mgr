#include "Core/dspch.hpp"

#include <exception>
#include <imgui.h>
#include <string>
#include <utility>

#include "Core/Notifications/Notifier.hpp"
#include "Demo/DemoCapabilities.hpp"

namespace DefectStudio::Demo
{
	DemoCapabilitiesPanel::DemoCapabilitiesPanel(
		Ref<CapabilityRegistry> capabilityRegistry,
		Ref<CapabilityService> capabilityService,
		Ref<Notifier> notifier)
		: m_CapabilityRegistry(std::move(capabilityRegistry)),
		  m_CapabilityService(std::move(capabilityService)),
		  m_Notifier(std::move(notifier))
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
		if (!m_CapabilityRegistry || !m_CapabilityService)
		{
			ImGui::TextUnformatted("Capability demo is not initialized.");
			return;
		}

		const auto capabilities = m_CapabilityRegistry->GetAll();

		ImGui::TextUnformatted("Local demo runtime capabilities");
		ImGui::Text("Registry locked: %s", m_CapabilityRegistry->IsLocked() ? "yes" : "no");
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

		ImGui::BeginDisabled(!m_CapabilityRegistry || m_CapabilityRegistry->IsLocked());
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
			if (m_CapabilityRegistry)
				m_CapabilityRegistry->LockAfterStartup();
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
		if (!m_CapabilityRegistry)
			return;

		const bool locked = m_CapabilityRegistry->IsLocked();
		m_CapabilityRegistry->RegisterCapability(CapabilityEntry{
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
		if (m_Notifier)
			m_Notifier->Info(std::move(title), std::move(message), NotificationCategory::Capability);
	}

	void DemoCapabilitiesPanel::notifyError(std::string title, std::string message)
	{
		if (m_Notifier)
			m_Notifier->Error(std::move(title), std::move(message), NotificationCategory::Capability);
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
