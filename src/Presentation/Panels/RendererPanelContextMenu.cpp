#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneVisibility.hpp"

namespace DefectStudio
{
	[[nodiscard]] static std::size_t ArrowIndex(const RendererWindowState &windowState, const SceneObjectId id)
	{
		const auto found = std::find_if(windowState.sceneArrows.begin(), windowState.sceneArrows.end(), [id](const auto &arrow) { return arrow.id == id; });
		return found == windowState.sceneArrows.end() ? windowState.sceneArrows.size() : static_cast<std::size_t>(std::distance(windowState.sceneArrows.begin(), found));
	}

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

	// Blender-style "adjust last operation" panel for a just-added SceneArrow - set active by every
	// Add Arrow entry point (Shift+A menu, right-click Add submenu, ObjectPropertiesPanel's own
	// "+ Add arrow" button). Anchored to THIS window's own viewport image (not the whole app), bottom
	// -left, so it reads as belonging to the arrow just added here. Closes itself - no explicit close
	// button needed beyond "Done" - the moment selection moves away from the arrow it was opened for
	// (Escape, clicking something else, deleting it), since at that point selectedSceneArrows no
	// longer matches sceneArrowQuickEditIndex exactly.
	void RendererPanel::renderSceneArrowQuickEditPanel(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (!windowState.sceneArrowQuickEditActive)
			return;
		if (windowState.sceneArrowQuickEditIndex >= windowState.sceneArrows.size() ||
			windowState.selectedSceneArrows.size() != 1 ||
			ArrowIndex(windowState, windowState.selectedSceneArrows[0]) != windowState.sceneArrowQuickEditIndex)
		{
			windowState.sceneArrowQuickEditActive = false;
			return;
		}

		ImGui::SetNextWindowPos(
			ImVec2(imageOrigin.x + 12.0f, imageOrigin.y + imageSize.y - 12.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
		constexpr ImGuiWindowFlags kFlags =
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
		// "###..." + windowId keeps this popup's ImGui identity distinct per structure window - same
		// reason renderStructureWindow's own imguiWindowLabel does, otherwise two windows with an
		// active quick-edit at once would collide onto the same popup.
		const std::string popupLabel = "Add Arrow###SceneArrowQuickEdit_" + windowState.windowId;
		if (ImGui::Begin(popupLabel.c_str(), nullptr, kFlags))
		{
			DrawSceneArrowEditor(
				windowState, windowState.sceneArrowQuickEditIndex, SceneArrowEditorMode::Compact,
				m_Layer.GetGlobalSettings());
			if (ImGui::Button("Done"))
				windowState.sceneArrowQuickEditActive = false;
		}
		ImGui::End();
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
			// Reuses the same Add Atom popup Shift+A opens (drawAddAtomPopup) rather than a separate
			// flow - mirrors the flag-setting Render() already does when addAtomPopupRequested comes
			// in via that event, just seeded with this menu's own click position instead of the 3D
			// cursor/origin default.
			if (ImGui::MenuItem("Atom..."))
			{
				m_AddAtomPopupRequested = true;
				m_AddAtomPopupWindowId = windowState.windowId;
				m_AddAtomPopupPosition = m_ContextMenuWorldPosition;
				m_AddAtomPopupFractional = false;
			}
			if (ImGui::MenuItem("Label"))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				RendererWindowState::FreeLabel label;
				label.id = windowState.sceneRegistry.AllocateObjectId();
				label.worldPosition = m_ContextMenuWorldPosition;
				windowState.freeLabels.push_back(std::move(label));
			}
			DrawFreeSegmentAddItems(windowState, m_ContextMenuWorldPosition);
			DrawFreePlaneAddItem(windowState, m_ContextMenuWorldPosition);
			const DrawSelectionDescription drawSelection = DescribeDrawSelection(windowState);
			if (ImGui::BeginMenu(drawSelection.menuLabel.c_str()))
			{
				DrawSegmentAddItems(windowState);
				DrawPlaneAddItem(windowState);
				ImGui::EndMenu();
			}
			DrawOrbitalAddMenu(windowState, m_ContextMenuWorldPosition);
			DrawScenePathDevAddMenu(windowState, m_ContextMenuWorldPosition);
			ImGui::Separator();
			DrawDefectAddItems(windowState, commandRegistry.get(), m_ContextMenuWorldPosition);
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
			runCommand("renderer.selection.copy");
		if (ImGui::MenuItem("Paste", "Ctrl+V"))
			runCommand("renderer.selection.paste");
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
			runCommand("renderer.selection.duplicate");

		// Copies from the first selected arrow (same "first selected wins" convention the 3D Cursor
		// submenu below already uses); pastes onto every selected arrow as one undo step. Two independent
		// clipboards (GetArrowGeometryClipboard/GetArrowStyleClipboard) rather than one tagged slot, so
		// Paste Geometry/Style are only enabled once that specific thing has actually been copied.
		const bool hasArrowSelection = !windowState.selectedSceneArrows.empty();
		if (ImGui::BeginMenu("Arrow", hasArrowSelection || GetArrowGeometryClipboard().has_value() ||
										   GetArrowStyleClipboard().has_value()))
		{
			if (ImGui::MenuItem("Copy Geometry", nullptr, false, hasArrowSelection))
				CopyArrowGeometry(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].style);
			if (ImGui::MenuItem("Copy Style", nullptr, false, hasArrowSelection))
				CopyArrowStyle(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].style);
			if (ImGui::MenuItem("Copy Geometry + Style", nullptr, false, hasArrowSelection))
			{
				const RendererWindowState::ArrowStyle &style =
					windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].style;
				CopyArrowGeometry(style);
				CopyArrowStyle(style);
			}

			ImGui::Separator();

			const bool canPasteGeometry = hasArrowSelection && GetArrowGeometryClipboard().has_value();
			const bool canPasteStyle = hasArrowSelection && GetArrowStyleClipboard().has_value();
			if (ImGui::MenuItem("Paste Geometry", nullptr, false, canPasteGeometry))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteArrowGeometry(windowState, windowState.selectedSceneArrows);
			}
			if (ImGui::MenuItem("Paste Style", nullptr, false, canPasteStyle))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteArrowStyle(windowState, windowState.selectedSceneArrows);
			}
			if (ImGui::MenuItem("Paste Geometry + Style", nullptr, false, canPasteGeometry && canPasteStyle))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteArrowGeometry(windowState, windowState.selectedSceneArrows);
				PasteArrowStyle(windowState, windowState.selectedSceneArrows);
			}

			ImGui::EndMenu();
		}

		// notes.txt pt. 15 - mirrors the "Arrow" submenu above, but LabelStyle has no separate
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

		if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
			runCommand("renderer.selection.delete");
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

			const bool hasOneArrowSelected = windowState.selectedSceneArrows.size() == 1;
			if (ImGui::MenuItem("Move to Arrow Start", nullptr, false, hasOneArrowSelected))
				publishCursor(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].start());
			if (ImGui::MenuItem("Move to Arrow End", nullptr, false, hasOneArrowSelected))
				publishCursor(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].end());

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}

} // namespace DefectStudio
