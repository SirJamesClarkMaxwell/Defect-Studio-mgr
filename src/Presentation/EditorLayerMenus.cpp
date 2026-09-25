#include "Core/dspch.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/MenuBarModel.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	void EditorLayer::renderMainMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		const auto executeCommand = [this](const char *commandId)
		{
			if (auto commandRegistry = m_CommandRegistry.lock())
			{
				CommandContext context;
				Result<CommandOutcome> result = commandRegistry->Execute(CommandID{commandId}, std::move(context));
				if (!result)
					DS_LOG_WARN("Command '{}' failed: {}", commandId, result.Error().technicalDetails);
			}
		};

		renderFileMenu(executeCommand);
		renderEditMenu(executeCommand);
		renderViewMenu();
		renderCommandMenu(executeCommand);
		renderToolsMenu();
		renderHelpMenu();
		renderNewSceneWindowButton();

		ImGui::EndMainMenuBar();
	}

	// Last item on the menu bar, past the menus: one click, one empty renderer window. No
	// structure, no domain registration - just a camera, the grid, and somewhere to put scene
	// objects that don't need atoms behind them.
	void EditorLayer::renderNewSceneWindowButton()
	{
		if (ImGui::MenuItem("+"))
		{
			if (auto rendererLayer = m_RendererLayer.lock())
				OpenEmptyRendererWindow(*rendererLayer, "Pusta scena");
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Nowe puste okno renderera");
	}

	void EditorLayer::renderFileMenu(const CommandMenuExecutor &executeCommand)
	{
		if (!ImGui::BeginMenu("Plik"))
			return;

		if (ImGui::MenuItem("Nowy"))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder({});
			if (picked && picked->has_value())
				createNewProject(picked->value());
		}
		if (ImGui::MenuItem("Otworz"))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder({});
			if (picked && picked->has_value())
				openProject(picked->value());
		}
		if (ImGui::MenuItem("Zapisz", "Ctrl+S"))
			persistCurrentRoots();
		if (ImGui::BeginMenu("Ostatnie projekty"))
		{
			std::vector<RecentProjectEntry> recents;
			std::string recentsError;
			(void)RecentProjectsIO::Load(RecentProjectsIO::DefaultFilePath(), recents, recentsError);
			if (recents.empty())
				ImGui::MenuItem("Brak ostatnich projektow", nullptr, false, false);
			else
			{
				for (const RecentProjectEntry &entry : recents)
				{
					if (ImGui::MenuItem(entry.projectDirectory.String().c_str()))
						openProject(entry.projectDirectory);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Eksport obrazu (PNG)..."))
		{
			if (auto panel = findPanel(m_ExportImagePanelId).lock())
			{
				panel->SetVisible(true);
				ImGui::SetWindowFocus(panel->GetTitle().c_str());
			}
		}
		if (ImGui::MenuItem("Wyjdz", "Ctrl+Shift+W"))
			executeCommand("app.quit");
		ImGui::EndMenu();
	}

	void EditorLayer::renderEditMenu(const CommandMenuExecutor &executeCommand)
	{
		if (!ImGui::BeginMenu("Edycja"))
			return;

		if (ImGui::MenuItem("Cofnij", "Ctrl+Z"))
			executeCommand("edit.undo");
		if (ImGui::MenuItem("Ponow", "Ctrl+Y"))
			executeCommand("edit.redo");
		ImGui::EndMenu();
	}

	void EditorLayer::renderViewMenu()
	{
		if (!ImGui::BeginMenu("Widok"))
			return;

		const std::vector<PanelId> panelIds = m_Panels.GetIds();
		std::vector<PanelMenuEntry> panelEntries;
		panelEntries.reserve(panelIds.size());
		for (const PanelId panelId : panelIds)
		{
			if (auto panel = findPanel(panelId).lock())
				panelEntries.push_back({panel->GetTitle(), panel->GetCategory()});
		}

		const std::vector<PanelMenuGroup> groups = BuildPanelMenuGroups(panelEntries);
		for (const PanelMenuGroup &group : groups)
		{
			if (!ImGui::BeginMenu(PanelCategoryName(group.category)))
				continue;

			for (const PanelId panelId : panelIds)
			{
				if (auto panel = findPanel(panelId).lock(); panel && panel->GetCategory() == group.category)
				{
					const std::string label = panel->GetTitle() + "##panel_" + std::to_string(panelId);
					bool visible = panel->IsVisible();
					if (ImGui::MenuItem(label.c_str(), nullptr, &visible))
						panel->SetVisible(visible);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Klonuj panel"))
		{
			for (const PanelMenuGroup &group : groups)
			{
				if (!ImGui::BeginMenu(PanelCategoryName(group.category)))
					continue;

				for (const PanelId panelId : panelIds)
				{
					if (auto panel = findPanel(panelId).lock(); panel && panel->GetCategory() == group.category)
					{
						const std::string label = panel->GetTitle() + "##panel_" + std::to_string(panelId);
						if (ImGui::MenuItem(label.c_str()))
							(void)m_Panels.Clone(panelId);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}

	void EditorLayer::renderCommandMenu(const CommandMenuExecutor &executeCommand)
	{
		if (!ImGui::BeginMenu("Polecenia"))
			return;

		if (auto commandRegistry = m_CommandRegistry.lock())
		{
			std::vector<CommandMenuGroup> groups = BuildCommandMenuGroups(commandRegistry->ListCommands());
			if (auto keymapResolver = m_KeymapResolver.lock())
			{
				const std::vector<KeyBinding> bindings = keymapResolver->ListBindings();
				const Ref<ContextManager> contextManager = m_ContextManager.lock();
				ContextManager emptyContext;
				const ContextManager &activeContext = contextManager ? *contextManager : emptyContext;
				for (CommandMenuGroup &group : groups)
				{
					for (CommandMenuEntry &command : group.commands)
					{
						for (const KeyBinding &binding : bindings)
						{
							if (!binding.enabled || binding.commandId.value != command.id)
								continue;
							if (!binding.when.Matches(activeContext))
								continue;
							command.chord = ToString(binding.chord);
							break;
						}
					}
				}
			}

			for (const CommandMenuGroup &group : groups)
			{
				if (!ImGui::BeginMenu(group.category.c_str()))
					continue;

				for (const CommandMenuEntry &command : group.commands)
				{
					const std::string label = command.name + "##" + command.id;
					if (ImGui::MenuItem(label.c_str(), command.chord.empty() ? nullptr : command.chord.c_str()))
						executeCommand(command.id.c_str());
					if (!command.description.empty() && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", command.description.c_str());
				}
				ImGui::EndMenu();
			}
		}

		ImGui::EndMenu();
	}

	void EditorLayer::renderToolsMenu()
	{
		if (!ImGui::BeginMenu("Narzedzia"))
			return;

		if (ImGui::MenuItem("Preferencje"))
		{
			if (auto panel = findPanel(m_SettingsPanelId).lock())
			{
				panel->SetVisible(true);
				ImGui::SetWindowFocus(panel->GetTitle().c_str());
			}
		}
		ImGui::EndMenu();
	}

	void EditorLayer::renderHelpMenu()
	{
		if (!ImGui::BeginMenu("Pomoc"))
			return;

		if (ImGui::MenuItem("Lista skrotow"))
		{
			if (auto panel = findPanel(m_SettingsPanelId).lock())
			{
				panel->SetVisible(true);
				ImGui::SetWindowFocus(panel->GetTitle().c_str());
			}
		}
		ImGui::EndMenu();
	}
} // namespace DefectStudio
