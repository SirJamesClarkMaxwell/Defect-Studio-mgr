#include "Core/dspch.hpp"

#include <functional>
#include <string>
#include <utility>

#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/EditorLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		class DockRegionCommand final : public ICommand
		{
		public:
			DockRegionCommand(std::function<void()> action, std::string description)
				: m_Action(std::move(action)), m_Description(std::move(description))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				m_Action();
				return {};
			}

			std::string Description() const override
			{
				return m_Description;
			}

		private:
			std::function<void()> m_Action;
			std::string m_Description;
		};

		[[nodiscard]] DockRectangle ToDockRectangle(const ImGuiDockNode &node)
		{
			return DockRectangle{
				glm::vec2{node.Pos.x, node.Pos.y},
				glm::vec2{node.Pos.x + node.Size.x, node.Pos.y + node.Size.y}};
		}
	}

	void EditorLayer::updateDockRegionPanelTitles()
	{
		m_LeftDockTitles.clear();
		m_BottomDockTitles.clear();
		m_RightDockTitles.clear();

		DockRectangle central{};
		if (ImGuiDockNode *centralNode = ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID))
			central = ToDockRectangle(*centralNode);

		for (const Entry &entry : m_Panels.Entries())
		{
			if (entry.panel == nullptr || !entry.panel->IsVisible())
				continue;

			ImGuiWindow *window = ImGui::FindWindowByName(entry.panel->GetTitle().c_str());
			if (window == nullptr || window->DockNode == nullptr || window->DockNode->IsFloatingNode())
				continue;

			const DockRegion region = ClassifyDockRegion(ToDockRectangle(*window->DockNode), central);
			m_DockRegionTracker.Observe(entry.panel->GetTitle(), region);
			switch (m_DockRegionTracker.RegionOf(entry.panel->GetTitle()))
			{
				case DockRegion::Left:
					m_LeftDockTitles.push_back(entry.panel->GetTitle());
					break;
				case DockRegion::Right:
					m_RightDockTitles.push_back(entry.panel->GetTitle());
					break;
				case DockRegion::Bottom:
					m_BottomDockTitles.push_back(entry.panel->GetTitle());
					break;
				default:
					break;
			}
		}
	}

	void EditorLayer::toggleDockRegion(DockRegion region)
	{
		updateDockRegionPanelTitles();

		DockRegionToggle *toggle = nullptr;
		std::vector<std::string> *titles = nullptr;
		switch (region)
		{
			case DockRegion::Left:
				toggle = &m_LeftDockRegion;
				titles = &m_LeftDockTitles;
				break;
			case DockRegion::Bottom:
				toggle = &m_BottomDockRegion;
				titles = &m_BottomDockTitles;
				break;
			case DockRegion::Right:
				toggle = &m_RightDockRegion;
				titles = &m_RightDockTitles;
				break;
			default:
				return;
		}

		applyDockRegionDecision(toggle->Toggle(*titles));
	}

	void EditorLayer::applyDockRegionDecision(const DockRegionToggle::Decision &decision)
	{
		for (const Entry &entry : m_Panels.Entries())
		{
			if (entry.panel == nullptr)
				continue;

			for (const std::string &title : decision.titles)
			{
				if (entry.panel->GetTitle() == title)
				{
					entry.panel->SetVisible(decision.makeVisible);
					break;
				}
			}
		}
	}

	void EditorLayer::registerDockRegionCommands()
	{
		auto commandRegistry = m_CommandRegistry.lock();
		if (commandRegistry == nullptr || commandRegistry->HasCommand(CommandID{"editor.toggle_left_dock_region"}))
			return;

		const auto registerCommand = [this, &commandRegistry](
			const char *id, const char *name, const char *description, DockRegion region) {
			auto result = commandRegistry->Register(
				CommandMeta{CommandID{id}, name, "Editor", description, {}, CommandFlags::None},
				[this, region, description](CommandContext &) -> Unique<ICommand> {
					return CreateUnique<DockRegionCommand>(
						[this, region] { toggleDockRegion(region); }, description);
				});
			if (!result)
				DS_LOG_WARN("{} command registration failed: {}", name, result.Error().technicalDetails);
		};

		registerCommand(
			"editor.toggle_left_dock_region",
			"Toggle Left Dock Region",
			"Hide or restore the panels docked on the left side of the viewport.",
			DockRegion::Left);
		registerCommand(
			"editor.toggle_bottom_dock_region",
			"Toggle Bottom Dock Region",
			"Hide or restore the panels docked below the viewport.",
			DockRegion::Bottom);
		registerCommand(
			"editor.toggle_right_dock_region",
			"Toggle Right Dock Region",
			"Hide or restore the panels docked on the right side of the viewport.",
			DockRegion::Right);
	}
} // namespace DefectStudio
