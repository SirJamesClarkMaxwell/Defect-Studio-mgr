#include "Core/dspch.hpp"

#include "Presentation/Panels/BondSettingsPanel.hpp"

#include <vector>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"

namespace DefectStudio
{
	BondSettingsPanel::BondSettingsPanel(
		RendererLayer &layer,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<DomainLayer> domainLayer,
		ElementPropertiesTable elementPropertiesTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_DomainLayer(std::move(domainLayer)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable))
	{
	}

	Ref<IPanel> BondSettingsPanel::Clone() const
	{
		return CreateRef<BondSettingsPanel>(*this);
	}

	void BondSettingsPanel::applySettings()
	{
		Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
		if (commandRegistry == nullptr || m_EditedForWindowId.empty())
			return;

		SetBondSettingsPayload payload;
		payload.windowId = m_EditedForWindowId;
		payload.settings = m_EditedSettings;
		CommandContext context;
		context.Set<SetBondSettingsPayload>("bond_edit.set_settings_payload", std::move(payload));
		Result<CommandOutcome> result = commandRegistry->Execute(CommandID{"renderer.bonds.set_settings"}, std::move(context));
		if (!result)
		{
			m_StatusMessage = "Rebuild failed: " + result.Error().technicalDetails;
			DS_LOG_WARN("Set bond settings failed: {}", result.Error().technicalDetails);
		}
		else
		{
			m_StatusMessage = "Bonds rebuilt.";
		}
	}

	void BondSettingsPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		// GetLastFocusedViewportWindowId, not GetFocusedViewportWindowId - the latter clears the
		// instant ImGui focus leaves the viewport, which is exactly what happens the moment this
		// panel's own fields are clicked (same fix already applied to ObjectPropertiesPanel/
		// ElectronicStructureSession for the same reason).
		const std::string &focusedWindowId = m_Layer.GetLastFocusedViewportWindowId();
		RendererWindowState *windowState = nullptr;
		if (!focusedWindowId.empty())
		{
			for (RendererWindowState &candidate : m_Layer.GetWindows())
			{
				if (candidate.windowId == focusedWindowId)
				{
					windowState = &candidate;
					break;
				}
			}
		}

		if (windowState == nullptr)
		{
			ImGui::TextDisabled("No renderer viewport focused.");
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		Ref<StructureRecord> domainRecord;
		if (domainLayer != nullptr)
		{
			Result<AtomEditTarget> target = ResolveAtomEditTarget(m_Layer, *domainLayer, windowState->windowId);
			if (target)
				domainRecord = target->record;
		}

		if (domainRecord == nullptr)
		{
			// A structure creation pane lands here: it is a preview, deliberately not registered in
			// the domain. Its cutoffs are edited in the New Structure panel, which owns the draft.
			ImGui::TextDisabled("Focused window has no domain structure.");
			ImGui::TextDisabled("A structure being created has its bond settings in the New Structure panel.");
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (m_EditedForWindowId != windowState->windowId)
		{
			m_EditedSettings = domainRecord->structure.bondSettings;
			m_EditedForWindowId = windowState->windowId;
			m_StatusMessage.clear();
		}

		ImGui::Checkbox("Auto-rebuild on change", &m_AutoRebuild);
		ImGui::Separator();

		const bool edited = DrawBondSettingsEditor(
			m_EditedSettings, domainRecord->structure.UniqueSpecies(), m_ElementPropertiesTable, m_Layer, m_EditorState);
		if (edited && m_AutoRebuild)
			applySettings();

		ImGui::Separator();
		if (ImGui::Button("Rebuild bonds"))
			applySettings();
		ImGui::SameLine();
		if (ImGui::Button("Reset to structure's current settings"))
		{
			m_EditedSettings = domainRecord->structure.bondSettings;
			m_StatusMessage.clear();
		}
		if (!m_StatusMessage.empty())
			ImGui::TextDisabled("%s", m_StatusMessage.c_str());

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
