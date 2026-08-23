#include "Core/dspch.hpp"

#include "Presentation/Panels/BondSettingsPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
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
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_DomainLayer(std::move(domainLayer))
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
			ImGui::TextDisabled("Focused window has no domain structure.");
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

		ImGui::TextWrapped(
			"Cutoff = scale x (covalent radius A + covalent radius B). A bond forms when two atoms "
			"are closer than their pair's cutoff.");
		ImGui::Separator();

		ImGui::SetNextItemWidth(160.0f);
		ImGui::DragFloat("Global cutoff scale", &m_EditedSettings.globalCutoffScale, 0.01f, 0.5f, 3.0f, "%.2f");
		ImGui::SameLine();
		ImGui::TextDisabled("(used for any pair without an override below)");

		ImGui::Separator();
		ImGui::TextUnformatted("Per-pair overrides");

		std::string pairToRemove;
		if (ImGui::BeginTable(
				"##BondPairOverrides", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Pair", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Scale");
			ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableHeadersRow();

			for (auto &[pairKey, scale] : m_EditedSettings.perPairCutoffOverride)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(pairKey.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::PushID(pairKey.c_str());
				ImGui::DragFloat("##Scale", &scale, 0.01f, 0.1f, 5.0f, "%.2f");
				ImGui::TableSetColumnIndex(2);
				if (ImGui::Button("Remove"))
					pairToRemove = pairKey;
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!pairToRemove.empty())
			m_EditedSettings.perPairCutoffOverride.erase(pairToRemove);

		ImGui::Separator();
		ImGui::TextUnformatted("Add override");
		ImGui::SetNextItemWidth(60.0f);
		ImGui::InputText("##PairFirst", m_NewPairFirst, sizeof(m_NewPairFirst));
		ImGui::SameLine();
		ImGui::TextUnformatted("-");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(60.0f);
		ImGui::InputText("##PairSecond", m_NewPairSecond, sizeof(m_NewPairSecond));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("##NewPairScale", &m_NewPairScale, 0.01f, 0.1f, 5.0f, "%.2f");
		ImGui::SameLine();
		const bool canAddPair = m_NewPairFirst[0] != '\0' && m_NewPairSecond[0] != '\0';
		ImGui::BeginDisabled(!canAddPair);
		if (ImGui::Button("Add"))
		{
			m_EditedSettings.perPairCutoffOverride[BondPairKey(m_NewPairFirst, m_NewPairSecond)] = m_NewPairScale;
			m_NewPairFirst[0] = '\0';
			m_NewPairSecond[0] = '\0';
		}
		ImGui::EndDisabled();

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
