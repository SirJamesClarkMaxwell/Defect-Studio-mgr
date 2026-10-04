#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathEditCommands.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		// Use the focused-window / sole-window rule for both commands
		// and context updates. A stale focused id must not silently target another window.
		template <typename Layer>
		[[nodiscard]] auto FindEditWindow(Layer &layer)
		{
			auto &windows = layer.GetWindows();
			const std::string &focusedId = layer.GetFocusedViewportWindowId();
			return focusedId.empty() && windows.size() == 1
				? windows.begin()
				: std::find_if(windows.begin(), windows.end(), [&focusedId](const RendererWindowState &window) {
					return !focusedId.empty() && window.windowId == focusedId;
				});
		}

		enum class EditAction
		{
			Toggle, Leave, Nodes, Segments, Whole, Extend, Insert, DeleteNodes, Reverse,
			HandleMenu, HandleFree, HandleAligned, HandleVector, HandleAuto, ReverseSelection
		};

		class ScenePathEditCommand final : public ICommand
		{
		public:
			ScenePathEditCommand(RendererLayer &layer, const EditAction action, std::string description)
				: m_Layer(layer), m_Action(action), m_Description(std::move(description))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				RendererLayer &layer = m_Layer.get();
				auto window = FindEditWindow(layer);
				if (window == layer.GetWindows().end() || window->modalTransform.has_value())
					return {};
				if (m_Action == EditAction::ReverseSelection)
				{
					if (window->pathEdit.IsActive())
						return {};
					const auto report = ReverseScenePaths(MakeWindowPathEditContext(*window), window->selectedScenePaths);
					return !report.AnyApplied() && !report.skipped.empty()
						? Result<void>{report.skipped.front().reason} : Result<void>{};
				}
				if (m_Action != EditAction::Toggle && !window->pathEdit.IsActive())
					return {};

				switch (m_Action)
				{
				case EditAction::Toggle:
					if (window->pathEdit.IsActive())
						window->pathEdit.Leave();
					else if (window->selectedScenePaths.size() == 1 && window->paths != nullptr &&
						window->paths->Store().Find(window->selectedScenePaths.front()) != nullptr)
						window->pathEdit.Enter(window->selectedScenePaths.front());
					break;
				case EditAction::Leave:
					window->pathEdit.Leave();
					break;
				case EditAction::Nodes:
					window->pathEdit.SetElementMode(PathElementMode::NodeHandle);
					break;
				case EditAction::Segments:
					window->pathEdit.SetElementMode(PathElementMode::Segment);
					break;
				case EditAction::Whole:
					window->pathEdit.SetElementMode(PathElementMode::WholePath);
					break;
				case EditAction::Extend:
				{
					const auto result = ExtendSelectedScenePathEnd(*window);
					return result ? Result<void>{} : result.Error();
				}
				case EditAction::Insert:
				{
					const auto result = InsertSelectedScenePathSegment(*window);
					return result ? Result<void>{} : result.Error();
				}
				case EditAction::DeleteNodes:
					return DeleteSelectedScenePathNodes(*window);
				case EditAction::Reverse:
					return ReverseEditedScenePath(*window);
				case EditAction::HandleMenu:
					window->pathHandleTypeMenuRequested = true;
					break;
				case EditAction::HandleFree:
					return SetSelectedScenePathHandleType(*window, BezierHandleType::Free);
				case EditAction::HandleAligned:
					return SetSelectedScenePathHandleType(*window, BezierHandleType::Aligned);
				case EditAction::HandleVector:
					return SetSelectedScenePathHandleType(*window, BezierHandleType::Vector);
				case EditAction::HandleAuto:
					return SetSelectedScenePathHandleType(*window, BezierHandleType::Auto);
				case EditAction::ReverseSelection:
					break;
				}
				return {};
			}

			[[nodiscard]] std::string Description() const override { return m_Description; }

		private:
			std::reference_wrapper<RendererLayer> m_Layer;
			EditAction m_Action;
			std::string m_Description;
		};
	} // namespace

	void RegisterScenePathObjectCommands(CommandRegistry &registry, RendererLayer &rendererLayer)
	{
		const auto result = registry.Register(
			CommandMeta{CommandID{"renderer.scene_path.reverse"}, "Renderer: Reverse selected paths", "Renderer",
				"Swap the start and end of every selected path in Object Mode.", {}, CommandFlags::None},
			[layer = std::ref(rendererLayer)](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<ScenePathEditCommand>(layer.get(), EditAction::ReverseSelection, "Reverse selected paths");
			});
		if (!result)
			DS_LOG_WARN("Path reverse command registration failed: {}", result.Error().technicalDetails);
	}

	void RegisterScenePathEditCommands(CommandRegistry &registry, RendererLayer &rendererLayer)
	{
		struct Definition
		{
			std::string id;
			std::string name;
			EditAction action;
		};
		const Definition definitions[] = {
			{"renderer.path_edit.toggle", "Path: Toggle Edit Mode", EditAction::Toggle},
			{"renderer.path_edit.leave", "Path: Leave Edit Mode", EditAction::Leave},
			{"renderer.path_edit.mode_nodes", "Path: Select nodes and handles", EditAction::Nodes},
			{"renderer.path_edit.mode_segments", "Path: Select segments", EditAction::Segments},
			{"renderer.path_edit.mode_whole", "Path: Select whole path", EditAction::Whole},
			{"renderer.path_edit.extend", "Path: Extend selected endpoint", EditAction::Extend},
			{"renderer.path_edit.insert", "Path: Insert node in selected segment", EditAction::Insert},
			{"renderer.path_edit.delete_nodes", "Path: Delete selected nodes", EditAction::DeleteNodes},
			{"renderer.path_edit.reverse", "Path: Reverse edited path", EditAction::Reverse},
			{"renderer.path_edit.handle_type_menu", "Path: Open handle type menu", EditAction::HandleMenu},
			{"renderer.path_edit.handle_free", "Path: Set handles to Free", EditAction::HandleFree},
			{"renderer.path_edit.handle_aligned", "Path: Set handles to Aligned", EditAction::HandleAligned},
			{"renderer.path_edit.handle_vector", "Path: Set handles to Vector", EditAction::HandleVector},
			{"renderer.path_edit.handle_auto", "Path: Set handles to Auto", EditAction::HandleAuto},
		};
		for (const Definition &definition : definitions)
		{
			const auto result = registry.Register(
				CommandMeta{CommandID{definition.id}, definition.name, "Renderer",
					definition.name, {}, CommandFlags::None},
				[layer = std::ref(rendererLayer), action = definition.action,
					description = definition.name](CommandContext &) -> Unique<ICommand> {
					return CreateUnique<ScenePathEditCommand>(layer.get(), action, description);
				});
			if (!result)
				DS_LOG_WARN("Path edit command '{}' registration failed: {}", definition.id, result.Error().technicalDetails);
		}
	}

	void UpdateScenePathEditContext(const RendererLayer &rendererLayer, ContextManager &contextManager)
	{
		const auto window = FindEditWindow(rendererLayer);
		contextManager.SetActive(kPathEditActiveContext,
			window != rendererLayer.GetWindows().end() && window->pathEdit.IsActive());
	}
} // namespace DefectStudio
