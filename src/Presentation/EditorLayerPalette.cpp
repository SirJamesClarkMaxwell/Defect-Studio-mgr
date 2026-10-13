#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Domain/DomainLayer.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/ProjectEvents.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PoscarWriter.hpp"
#include "IO/POTCARWriter.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/EnergeticsIO.hpp"
#include "IO/FnvCorrectionsIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/ProjectSceneSave.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/BondSettingsPanel.hpp"
#include "Presentation/Panels/NewStructureWizardPanel.hpp"
#include "Presentation/Panels/CalculatorConsolePanel.hpp"
#include "Presentation/Panels/ElectronicStructurePanel.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/SceneDensityLoader.hpp"
#include "Presentation/Panels/ExportImagePanel.hpp"
#include "Presentation/Panels/OccupationDiagramPanel.hpp"
#include "Presentation/Panels/DisplacementComparisonPanel.hpp"
#include "Presentation/Panels/GroupTheoryPanel.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/DefectEnergeticsSession.hpp"
#include "Presentation/Panels/CalculationSummarySession.hpp"
#include "Presentation/Panels/ElementCatalogPanel.hpp"
#include "Presentation/Panels/ObjectPropertiesPanel.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/MaterialsCollectionPanel.hpp"
#include "Presentation/Panels/StructureCreationTabsPanel.hpp"
#include "Presentation/Panels/StructureHubPanel.hpp"
#include "Presentation/Panels/TerminalPanel.hpp"
#include "Presentation/Panels/TextEditorPanel.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"

namespace DefectStudio
{
	struct PaletteCommandRow
	{
		CommandID id;
		std::string name;
		std::string category;
		std::string description;
		std::string shortcut;
		bool enabled = true;
	};

	struct PaletteInputState
	{
		int navigationDelta = 0;
	};

	[[nodiscard]] std::string toLowerCopy(std::string_view value)
	{
		std::string result(value);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return result;
	}

	[[nodiscard]] bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
	{
		if (needle.empty())
			return true;
		const std::string lowerHaystack = toLowerCopy(haystack);
		const std::string lowerNeedle = toLowerCopy(needle);
		return lowerHaystack.find(lowerNeedle) != std::string::npos;
	}

	[[nodiscard]] std::string findShortcutForCommand(
		const CommandID &id,
		const KeymapResolver *resolver,
		const ContextManager *contextManager)
	{
		if (resolver == nullptr)
			return {};

		ContextManager emptyContext;
		const ContextManager &activeContext = contextManager == nullptr ? emptyContext : *contextManager;
		for (const KeyBinding &binding : resolver->ListBindings())
		{
			if (!binding.enabled || binding.commandId.value != id.value || !binding.when.Matches(activeContext))
				continue;
			return ToString(binding.chord);
		}
		return {};
	}

	[[nodiscard]] bool matchesPaletteQuery(const PaletteCommandRow &row, std::string_view query)
	{
		return containsIgnoreCase(row.name, query)
			|| containsIgnoreCase(row.category, query)
			|| containsIgnoreCase(row.description, query)
			|| containsIgnoreCase(row.id.value, query)
			|| containsIgnoreCase(row.shortcut, query);
	}

	int commandPaletteInputCallback(ImGuiInputTextCallbackData *data)
	{
		auto *state = static_cast<PaletteInputState *>(data->UserData);
		if (state == nullptr || data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
			return 0;

		if (data->EventKey == ImGuiKey_UpArrow)
			state->navigationDelta = -1;
		else if (data->EventKey == ImGuiKey_DownArrow)
			state->navigationDelta = 1;
		return 0;
	}


	void EditorLayer::renderCommandPalettePopup()
	{
		if (m_CommandPaletteOpenRequested)
		{
			ImGui::OpenPopup("Command Palette");
			m_CommandPaletteOpenRequested = false;
			m_CommandPaletteSelection = 0;
		}

		bool open = true;
		if (!ImGui::BeginPopupModal("Command Palette", &open, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		auto commandRegistry = m_CommandRegistry.lock();
		auto commandService = m_CommandService.lock();
		auto keymapResolver = m_KeymapResolver.lock();
		auto contextManager = m_ContextManager.lock();
		if (commandRegistry == nullptr || commandService == nullptr)
		{
			ImGui::TextDisabled("Command runtime unavailable.");
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();

		PaletteInputState inputState;
		ImGui::SetNextItemWidth(520.0f);
		const bool enterPressed = ImGui::InputTextWithHint(
			"##command_palette_search",
			"Search commands",
			m_CommandPaletteSearchBuffer.data(),
			m_CommandPaletteSearchBuffer.size(),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
			commandPaletteInputCallback,
			&inputState);

		std::vector<PaletteCommandRow> rows;
		for (const CommandMeta &meta : commandRegistry->ListCommands())
		{
			if (HasFlag(meta.flags, CommandFlags::HiddenFromPalette))
				continue;

			PaletteCommandRow row;
			row.id = meta.id;
			row.name = meta.name;
			row.category = meta.category;
			row.description = meta.description;
			row.shortcut = findShortcutForCommand(
				meta.id,
				keymapResolver.get(),
				contextManager.get());
			row.enabled = commandService->CanExecute(meta.id).HasValue();
			if (matchesPaletteQuery(row, m_CommandPaletteSearchBuffer.data()))
				rows.push_back(std::move(row));
		}

		std::sort(rows.begin(), rows.end(), [](const PaletteCommandRow &lhs, const PaletteCommandRow &rhs) {
			if (lhs.category == rhs.category)
				return lhs.name < rhs.name;
			return lhs.category < rhs.category;
		});

		if (m_CommandPaletteSelection >= static_cast<int>(rows.size()))
			m_CommandPaletteSelection = std::max(0, static_cast<int>(rows.size()) - 1);

		if (inputState.navigationDelta != 0 && !rows.empty())
			m_CommandPaletteSelection = std::clamp(
				m_CommandPaletteSelection + inputState.navigationDelta,
				0,
				static_cast<int>(rows.size()) - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();

		ImGui::Separator();
		if (ImGui::BeginChild("##command_palette_results", ImVec2(640.0f, 320.0f), true))
		{
			for (int index = 0; index < static_cast<int>(rows.size()); ++index)
			{
				const PaletteCommandRow &row = rows[static_cast<std::size_t>(index)];
				const std::string label = row.name
					+ (row.shortcut.empty() ? "" : "    " + row.shortcut)
					+ "##" + row.id.value;
				const bool selected = index == m_CommandPaletteSelection;
				if (selected)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.14f, 0.17f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.10f, 0.12f, 0.15f, 1.0f));
				}
				ImGui::BeginDisabled(!row.enabled);
				if (ImGui::Selectable(label.c_str(), selected))
				{
					executeCommandFromPalette(row.id);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				if (selected)
				{
					if (inputState.navigationDelta != 0)
						ImGui::SetScrollHereY(0.5f);
					ImGui::PopStyleColor(3);
				}
				if (!row.description.empty())
					ImGui::TextDisabled("%s", row.description.c_str());
			}

			if (rows.empty())
				ImGui::TextDisabled("No commands found.");
		}
		ImGui::EndChild();

		if (enterPressed && !rows.empty())
		{
			const PaletteCommandRow &row = rows[static_cast<std::size_t>(m_CommandPaletteSelection)];
			if (row.enabled)
			{
				executeCommandFromPalette(row.id);
				ImGui::CloseCurrentPopup();
			}
		}

		if (!open)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void EditorLayer::executeCommandFromPalette(const CommandID &id)
	{
		auto commandService = m_CommandService.lock();
		if (commandService == nullptr)
			return;

		CommandContext context(ContextID{"editor.command_palette"});
		context.SetSource("EditorLayer command palette");
		auto result = commandService->Execute(id, std::move(context));
		if (!result)
			DS_LOG_WARN("EditorLayer command palette failed: {}", result.Error().technicalDetails);
	}

	void EditorLayer::onOpenCommandPaletteRequested(const CoreEvents::OpenCommandPaletteRequested &)
	{
		m_CommandPaletteOpenRequested = true;
	}
} // namespace DefectStudio
