#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ViewportAddMenu.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneVisibility.hpp"

namespace DefectStudio
{
	glm::vec3 RendererPanel::computeViewportWorldPosition(const RendererWindowState &windowState, float relX, float relY) const
	{
		return ComputeViewportWorldPosition(windowState, relX, relY);
	}

	// 3D cursor tool click - see computeViewportWorldPosition for the hit/plane logic.
	bool RendererPanel::handleCursor3DPlacement(RendererWindowState &windowState, float relX, float relY)
	{
		if (!windowState.camera)
			return false;

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return false;

		RendererEvents::Viewport::Cursor3DSetPositionRequested event;
		event.windowId = windowState.windowId;
		event.position = computeViewportWorldPosition(windowState, relX, relY);
		eventBus->Publish(event);
		return true;
	}

	// Right-click viewport context menu. Delete/Hide/Duplicate/Copy/Paste/Select All route through
	// CommandRegistry using the SAME command IDs their keybindings use (identical behaviour, undo
	// history stays consistent); Clear Selection and the 3D-cursor items are cheap enough to publish
	// directly, matching the rest of this panel's style for non-domain, non-undoable state.
	void RendererPanel::renderViewportContextMenu(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		(void)hovered;

		// Opened by hand instead of with BeginPopupContextItem. That helper hangs off whichever item
		// was submitted last, and by the time this runs the drag-drop target, the gizmo chain and the
		// pin/label/arrow/orbital handlers have all had their chance to submit items of their own -
		// so the menu attached itself to one of those and stopped opening over the viewport at all.
		// The image rect does not move, so the hit test uses that.
		constexpr const char *kContextMenuId = "##RendererViewportContextMenu";
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool overImage = mouse.x >= imageOrigin.x && mouse.y >= imageOrigin.y &&
			mouse.x < imageOrigin.x + imageSize.x && mouse.y < imageOrigin.y + imageSize.y;
		// Released, not clicked, and only when the button barely moved - a right-drag is a camera
		// move (ApplyViewportInputNavigation), and it must not end in a popup.
		const bool rightDragged = ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] > 25.0f;
		if (overImage && ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			!rightDragged && !ImGui::IsPopupOpen(kContextMenuId))
			ImGui::OpenPopup(kContextMenuId);

		if (!ImGui::BeginPopup(kContextMenuId))
			return;

		// Recorded here, on the frame the popup actually appears, rather than in a separate
		// "hovered && right-clicked" branch outside it. Those were two different conditions deciding
		// one event, and whenever they disagreed - a gizmo or overlay under the cursor eating the
		// panel's hover flag while ImGui still opened the menu - this field silently kept its
		// initial value and every object added from the menu (Atom, Label, Arrow, Orbital) landed at
		// the world origin, off-screen for anything but a structure sitting at 0,0,0.
		// GetMousePosOnOpeningCurrentPopup is ImGui's own record of where the popup was opened, so
		// the two can no longer drift apart.
		if (ImGui::IsWindowAppearing())
		{
			const ImVec2 openedAt = ImGui::GetMousePosOnOpeningCurrentPopup();
			m_ContextMenuWorldPosition =
				computeViewportWorldPosition(windowState, openedAt.x - imageOrigin.x, openedAt.y - imageOrigin.y);
		}

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
		const bool hasSelection = !windowState.selectedAtomIndices.empty();

		auto runCommand = [&](const char *commandId)
		{
			if (commandRegistry == nullptr)
				return;
			Result<CommandOutcome> result = commandRegistry->Execute(CommandID{commandId}, {});
			if (!result)
				DS_LOG_WARN("Viewport context menu command '{}' failed: {}", commandId, result.Error().technicalDetails);
		};

		if (ImGui::BeginMenu("Add"))
		{
			DrawSceneAddMenu(windowState, m_CommandRegistry, m_ContextMenuWorldPosition, eventBus);
			ImGui::EndMenu();
		}

		ImGui::Separator();

		const bool pathSelection = !windowState.selectedScenePaths.empty();
		const auto editPathOrAtoms = [&](SceneObjectEditAction action, const char *commandId) {
			if (pathSelection || (action == SceneObjectEditAction::Paste && !hasSelection &&
				CanExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, action)))
				ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, action);
			else
				runCommand(commandId);
		};
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection || pathSelection))
			editPathOrAtoms(SceneObjectEditAction::Copy, "renderer.selection.copy");
		if (ImGui::MenuItem("Paste", "Ctrl+V"))
			editPathOrAtoms(SceneObjectEditAction::Paste, "renderer.selection.paste");
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection || pathSelection))
			editPathOrAtoms(SceneObjectEditAction::Duplicate, "renderer.selection.duplicate");

		// LabelStyle has no separate
		// geometry to split out, so just one Copy/Paste Style pair. Applies to whichever label kind is
		// selected (pinned bond/angle labels and free labels share this one clipboard, same as the
		// "Selected labels" bulk editor in ObjectPropertiesPanel).
		const bool hasLabelSelection =
			!windowState.selectedPinnedMeasurements.empty() || !windowState.selectedFreeLabels.empty();
		if (ImGui::BeginMenu("Label", hasLabelSelection || GetLabelStyleClipboard().has_value()))
		{
			if (ImGui::MenuItem("Copy Style", nullptr, false, hasLabelSelection))
			{
				const RendererWindowState::LabelStyle *style = nullptr;
				if (!windowState.selectedPinnedMeasurements.empty())
				{
					if (const auto *pin = FindAnnotation(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements.front()))
						style = &pin->style;
				}
				else if (const auto *label = FindAnnotation(windowState.freeLabels, windowState.selectedFreeLabels.front()))
				{
					style = &label->style;
				}
				if (style != nullptr)
					CopyLabelStyle(*style);
			}
			const bool canPasteLabelStyle = hasLabelSelection && GetLabelStyleClipboard().has_value();
			if (ImGui::MenuItem("Paste Style", nullptr, false, canPasteLabelStyle))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteLabelStyle(windowState, windowState.selectedPinnedMeasurements, windowState.selectedFreeLabels);
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Delete", "Del", false, hasSelection || pathSelection))
			editPathOrAtoms(SceneObjectEditAction::Delete, "renderer.selection.delete");
		DrawDefectFrameMenu(windowState, commandRegistry.get());
		// H hides every selected kind, not only atoms - see Renderer/Scene/SceneVisibility.hpp.
		if (ImGui::MenuItem("Hide", "H", false, AnySceneObjectSelected(windowState)))
			runCommand("renderer.selection.hide");

		if (ImGui::BeginMenu("Change type", hasSelection))
		{
			static char speciesBuffer[8] = "";
			ImGui::SetNextItemWidth(80.0f);
			const bool enterPressed = ImGui::InputText(
				"##ChangeTypeInput", speciesBuffer, sizeof(speciesBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			const bool applyPressed = ImGui::SmallButton("Apply");
			if ((enterPressed || applyPressed) && speciesBuffer[0] != '\0' && commandRegistry != nullptr)
			{
				ChangeAtomTypePayload payload;
				payload.windowId = windowState.windowId;
				payload.species = speciesBuffer;
				CommandContext context;
				context.Set<ChangeAtomTypePayload>("atom_edit.change_type_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.selection.change_type"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Change atom type failed: {}", result.Error().technicalDetails);
				speciesBuffer[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Select All", "Ctrl+A"))
			runCommand("renderer.selection.select_all");
		if (ImGui::MenuItem("Invert Selection", "I"))
			runCommand("renderer.selection.invert");
		if (ImGui::MenuItem("Clear Selection", nullptr, false, hasSelection) && eventBus != nullptr)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.additive = false;
			eventBus->Publish(event);
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("3D Cursor"))
		{
			auto publishCursor = [&](const glm::vec3 &position)
			{
				if (eventBus == nullptr)
					return;
				RendererEvents::Viewport::Cursor3DSetPositionRequested event;
				event.windowId = windowState.windowId;
				event.position = position;
				eventBus->Publish(event);
			};

			if (ImGui::MenuItem("Set Here"))
				publishCursor(m_ContextMenuWorldPosition);

			if (ImGui::MenuItem("Move to Selection Center", nullptr, false, hasSelection))
			{
				glm::vec3 centroid(0.0f);
				for (const std::size_t atomIndex : windowState.selectedAtomIndices)
					centroid += windowState.structure.atoms[atomIndex].cartesianPosition;
				centroid /= static_cast<float>(windowState.selectedAtomIndices.size());
				publishCursor(centroid);
			}
			if (ImGui::MenuItem("Move to First Selected", nullptr, false, hasSelection))
				publishCursor(windowState.structure.atoms[windowState.selectedAtomIndices.front()].cartesianPosition);
			if (ImGui::MenuItem("Move to Last Selected", nullptr, false, hasSelection))
				publishCursor(windowState.structure.atoms[windowState.selectedAtomIndices.back()].cartesianPosition);
			if (ImGui::MenuItem("Move to Origin"))
				publishCursor(glm::vec3(0.0f));

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}

} // namespace DefectStudio
