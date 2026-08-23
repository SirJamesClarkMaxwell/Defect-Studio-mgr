#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanel.hpp"

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"

namespace DefectStudio
{
	ObjectPropertiesPanel::ObjectPropertiesPanel(
		RendererLayer &layer, WeakRef<CommandRegistry> commandRegistry, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Layer(layer), m_CommandRegistry(std::move(commandRegistry))
	{
	}

	Ref<IPanel> ObjectPropertiesPanel::Clone() const
	{
		return CreateRef<ObjectPropertiesPanel>(*this);
	}

	void ObjectPropertiesPanel::Render()
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

		const std::string &focusedWindowId = m_Layer.GetFocusedViewportWindowId();
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
		}
		else if (windowState->selectedAtomIndices.size() != 1)
		{
			ImGui::TextDisabled("Select exactly 1 atom to edit its properties.");
		}
		else
		{
			const std::size_t atomIndex = windowState->selectedAtomIndices.front();
			if (atomIndex >= windowState->structure.atoms.size())
			{
				ImGui::TextDisabled("Selected atom is no longer valid.");
			}
			else
			{
				const RendererAtomData &atom = windowState->structure.atoms[atomIndex];
				ImGui::Text("Element: %s (index %zu)", atom.element.c_str(), atomIndex);
				ImGui::Separator();
				ImGui::Text("Position (cartesian, A)");

				glm::vec3 position = atom.cartesianPosition;
				bool committed = false;
				ImGui::PushItemWidth(140.0f);
				ImGui::InputFloat("X", &position.x, 0.0f, 0.0f, "%.4f");
				committed |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputFloat("Y", &position.y, 0.0f, 0.0f, "%.4f");
				committed |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputFloat("Z", &position.z, 0.0f, 0.0f, "%.4f");
				committed |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::PopItemWidth();

				if (committed)
				{
					Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
					if (commandRegistry != nullptr)
					{
						GizmoTransformPayload payload;
						payload.windowId = windowState->windowId;
						payload.atomIndices = {atomIndex};
						payload.afterPositions = {position};
						payload.description = "Set atom position";

						CommandContext context;
						context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
						Result<CommandOutcome> result =
							commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
						if (!result)
							DS_LOG_WARN("Set atom position failed: {}", result.Error().technicalDetails);
					}
				}
			}
		}

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
