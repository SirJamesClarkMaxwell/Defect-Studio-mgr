#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewportToolbars.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	const std::array<ViewportSelectionModeEntry, 5> &ViewportSelectionModeEntries()
	{
		static constexpr std::array<ViewportSelectionModeEntry, 5> entries = {{
			{"Atoms only", "Ctrl+1", "tool-mode-atoms.png", true, false, false},
			{"Atoms + Bonds", "Ctrl+2", "tool-mode-atoms-bonds.png", true, true, false},
			{"Bonds + Labels", "Ctrl+3", "tool-mode-bonds-labels.png", false, true, true},
			{"All", "Ctrl+4", "tool-mode-all.png", true, true, true},
			{"Labels only", "Ctrl+5", "tool-mode-labels.png", false, false, true},
		}};
		return entries;
	}

	std::size_t ResolveViewportSelectionModeEntryIndex(
		const bool pickAtoms, const bool pickBonds, const bool pickLabels)
	{
		const auto &entries = ViewportSelectionModeEntries();
		for (std::size_t index = 0; index < entries.size(); ++index)
		{
			const ViewportSelectionModeEntry &entry = entries[index];
			if (entry.pickAtoms == pickAtoms && entry.pickBonds == pickBonds && entry.pickLabels == pickLabels)
				return index;
		}
		return 3; // The only masks produced by the keymap are listed above; use All as a safe fallback.
	}

	// VESTA-style vertical icon strip along the viewport's left edge: one click each for the tool
	// modes that would otherwise only be reachable via keyboard shortcut (G/R/S/B/C/M/Shift+M) or
	// not at all (3D cursor placement, "nothing"/idle tool). Publishes the same events those
	// shortcuts do rather than going through CommandRegistry, matching this file's existing style
	// (see DrawViewportToolbar's iconButton/queueTransition in ViewportToolbars.cpp) - none of these need undo or a
	// command-palette entry of their own beyond what's already registered for the keybindings.
	void DrawViewportVerticalToolbar(RendererWindowState &windowState, RendererLayer &layer)
	{
		Ref<EventBus> eventBus = layer.GetEventBus();
		if (eventBus == nullptr)
			return;

		const float uiScale = ImGui::GetIO().FontGlobalScale / kViewportToolbarFontScaleBaseline;
		const float iconExtentBase = std::clamp(layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f);
		const float iconExtent = iconExtentBase * uiScale;
		const ImVec2 buttonSize(iconExtent, iconExtent);
		const float columnWidth = iconExtent + ImGui::GetStyle().WindowPadding.x * 2.0f ;//+ 4.0f;
		const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(
			ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x * uiScale, itemSpacing.y * uiScale));

		ImGui::BeginChild(
			"##ViewportVerticalToolbar", ImVec2(columnWidth, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_NoScrollbar);

		// Centers each button horizontally within whatever width BeginChild actually gave the child
		// this frame, rather than trusting columnWidth's padding math to land exactly on center - this
		// is correct by construction regardless of where any extra horizontal slack comes from.
		const float buttonIndentX = std::max(0.0f, (ImGui::GetContentRegionAvail().x - iconExtent) * 0.5f);

		auto toolButton = [&](const char *id, const char *iconFileName, const char *fallback, const char *tooltip, bool active) -> bool
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonIndentX);

			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

			// ImageButton's clickable footprint is (size + FramePadding*2), unlike Button() where the
			// given size IS the footprint - with the style's default padding still applied, icon
			// buttons rendered visibly bigger than the plain-text fallback buttons (1/2/3/4, before any
			// icon existed for those) even though both passed the same buttonSize. Zeroing padding here
			// makes both paths occupy exactly buttonSize, so the column is a uniform grid.
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

			bool pressed = false;
			const RendererToolbarIconTexture *icon = layer.GetToolbarIcon(iconFileName);
			if (icon != nullptr && icon->rendererId != 0)
			{
				const ImTextureRef textureRef(reinterpret_cast<void *>(static_cast<uintptr_t>(icon->rendererId)));
				pressed = ImGui::ImageButton(
					id,
					textureRef,
					buttonSize,
					ImVec2(0.0f, 0.0f),
					ImVec2(1.0f, 1.0f),
					ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
					ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			}
			else
			{
				// No icon asset for this button yet - every button in this column is the same
				// icon-sized square regardless of its fallback label's length, so the column reads as
				// one clean aligned grid instead of a ragged mix of widths. Fallback labels are kept to
				// 2-3 characters precisely so they fit inside that square instead of clipping.
				pressed = ImGui::Button(fallback, buttonSize);
			}
			ImGui::PopStyleVar();

			if (active)
				ImGui::PopStyleColor();

			if (tooltip != nullptr && tooltip[0] != '\0' &&
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
			return pressed;
		};

		auto publishToolToggle = [&](SelectionToolMode tool)
		{
			RendererEvents::Viewport::SelectionToolToggleRequested event;
			event.windowId = windowState.windowId;
			event.tool = tool;
			eventBus->Publish(event);
		};

		if (toolButton(
				"##ToolNone", "tool-select.png", "Sel", "Selection tool - plain click-select, no drag tool active",
				windowState.activeSelectionTool == SelectionToolMode::None))
		{
			publishToolToggle(SelectionToolMode::None);
		}

		if (toolButton(
				"##ToolCursor3D", "tool-cursor3d.png", "3D", "3D cursor - click in the viewport to place it",
				windowState.activeSelectionTool == SelectionToolMode::Cursor3D))
		{
			publishToolToggle(SelectionToolMode::Cursor3D);
		}

		if (toolButton(
				"##ToolText", "tool-text.png", "T", "Text (TeX: V_B, x^2, \\alpha)",
				windowState.activeSelectionTool == SelectionToolMode::Text))
		{
			publishToolToggle(SelectionToolMode::Text);
		}

		ImGui::Spacing();

		if (toolButton(
				"##ToolMeasureBond", "tool-measure-bond.png", "Len", "Measure bond length - click any 2 atoms (M)",
				windowState.activeSelectionTool == SelectionToolMode::MeasureBond))
		{
			publishToolToggle(SelectionToolMode::MeasureBond);
		}

		if (toolButton(
				"##ToolMeasureAngle", "tool-measure-angle.png", "Ang", "Measure angle - click any 3 atoms (Shift+M)",
				windowState.activeSelectionTool == SelectionToolMode::MeasureAngle))
		{
			publishToolToggle(SelectionToolMode::MeasureAngle);
		}

		ImGui::Spacing();

		auto publishGizmoOperation = [&](GizmoOperation operation)
		{
			RendererEvents::Viewport::GizmoOperationRequested event;
			event.windowId = windowState.windowId;
			event.operation = operation;
			eventBus->Publish(event);
		};

		if (toolButton(
				"##ToolMove", "tool-move.png", "Mov", "Move (G)", windowState.gizmoOperation == GizmoOperation::Translate))
			publishGizmoOperation(GizmoOperation::Translate);

		if (toolButton(
				"##ToolRotate", "tool-rotate.png", "Rot", "Rotate (R)", windowState.gizmoOperation == GizmoOperation::Rotate))
			publishGizmoOperation(GizmoOperation::Rotate);

		if (toolButton(
				"##ToolScale", "tool-scale.png", "Scl", "Scale (S)", windowState.gizmoOperation == GizmoOperation::Scale))
			publishGizmoOperation(GizmoOperation::Scale);

		ImGui::Spacing();

		if (toolButton(
				"##ToolBoxSelect", "tool-box-select.png", "Box", "Box select (B)",
				windowState.activeSelectionTool == SelectionToolMode::Box))
		{
			publishToolToggle(SelectionToolMode::Box);
		}

		if (toolButton(
				"##ToolCircleSelect", "tool-circle-select.png", "Cir", "Circle select (C)",
				windowState.activeSelectionTool == SelectionToolMode::Circle))
		{
			publishToolToggle(SelectionToolMode::Circle);
		}

		ImGui::Spacing();

		auto publishSelectionMode = [&](bool pickAtoms, bool pickBonds, bool pickLabels)
		{
			RendererEvents::Viewport::SelectionModeSetRequested event;
			event.windowId = windowState.windowId;
			event.pickAtoms = pickAtoms;
			event.pickBonds = pickBonds;
			event.pickLabels = pickLabels;
			eventBus->Publish(event);
		};

		const auto &selectionModeEntries = ViewportSelectionModeEntries();
		const std::size_t currentSelectionModeIndex = ResolveViewportSelectionModeEntryIndex(
			windowState.pickAtoms, windowState.pickBonds, windowState.pickLabels);
		const ViewportSelectionModeEntry &currentSelectionMode = selectionModeEntries[currentSelectionModeIndex];
		const char fallback[] = {currentSelectionMode.shortcut.back(), '\0'};
		const std::string selectionModeTooltip = "Selection mode: " + std::string(currentSelectionMode.name) +
			" (" + std::string(currentSelectionMode.shortcut) + ")";
		const bool selectionModePressed = toolButton(
			"##SelectionModeMenu", currentSelectionMode.iconFileName.data(), fallback,
			selectionModeTooltip.c_str(), false);
		const ImVec2 selectionModePopupPosition(
			ImGui::GetItemRectMax().x + 4.0f * uiScale, ImGui::GetItemRectMin().y);
		if (selectionModePressed)
			ImGui::OpenPopup("##SelectionModePopup");
		ImGui::SetNextWindowPos(selectionModePopupPosition, ImGuiCond_Appearing);
		if (ImGui::BeginPopup("##SelectionModePopup"))
		{
			const float popupIconExtent = iconExtent * 0.70f;
			for (std::size_t index = 0; index < selectionModeEntries.size(); ++index)
			{
				const ViewportSelectionModeEntry &entry = selectionModeEntries[index];
				ImGui::PushID(static_cast<int>(index));
				if (const RendererToolbarIconTexture *icon = layer.GetToolbarIcon(entry.iconFileName.data());
					icon != nullptr && icon->rendererId != 0)
				{
					const ImTextureRef textureRef(
						reinterpret_cast<void *>(static_cast<uintptr_t>(icon->rendererId)));
					ImGui::Image(textureRef, ImVec2(popupIconExtent, popupIconExtent));
					ImGui::SameLine();
				}
				if (ImGui::MenuItem(
						entry.name.data(), entry.shortcut.data(), index == currentSelectionModeIndex))
				{
					publishSelectionMode(entry.pickAtoms, entry.pickBonds, entry.pickLabels);
				}
				ImGui::PopID();
			}
			ImGui::EndPopup();
		}

		ImGui::Spacing();

		// Quick-launch for the Atoms Displacement panel, pinning THIS window as the reference -
		// unambiguous by construction (no "which viewport is focused" guessing), see
		// DisplacementComparisonPanel::OpenForWindow.
		if (toolButton(
				"##ToolDisplacementComparison", "tool-displacement.png", "Cmp",
				"Compare this structure against another (atoms-displacement arrows)", false))
		{
			RendererEvents::Viewport::DisplacementComparisonPanelRequested event;
			event.windowId = windowState.windowId;
			eventBus->Publish(event);
		}

		// The add-object tools live at the bottom of the strip and the selection tools at the top:
		// picking is what a viewport does all day, adding an object is the occasional act, and the
		// top of a column is where the hand goes first.
		ImGui::Spacing();

		const bool segmentPressed = toolButton(
			"##ToolAddSegment", "tool-add-arrow.png", "Arr",
			"Add line or arrow at the 3D cursor or between two selected atoms", false);
		const ImVec2 segmentPopupPosition(
			ImGui::GetItemRectMax().x + 4.0f * uiScale, ImGui::GetItemRectMin().y);
		if (segmentPressed)
			ImGui::OpenPopup("##AddSegmentPopup");
		ImGui::SetNextWindowPos(segmentPopupPosition, ImGuiCond_Appearing);
		if (ImGui::BeginPopup("##AddSegmentPopup"))
		{
			DrawFreeSegmentAddItems(windowState, windowState.cursor3DPosition);
			ImGui::SeparatorText("Z zaznaczenia");
			DrawSegmentAddItems(windowState);
			ImGui::EndPopup();
		}

		const bool planePressed = toolButton(
			"##ToolAddPlane", "tool-add-plane.png", "Pln",
			"Add plane at the 3D cursor or fit it to selected atoms", false);
		const ImVec2 planePopupPosition(
			ImGui::GetItemRectMax().x + 4.0f * uiScale, ImGui::GetItemRectMin().y);
		if (planePressed)
			ImGui::OpenPopup("##AddPlanePopup");
		ImGui::SetNextWindowPos(planePopupPosition, ImGuiCond_Appearing);
		if (ImGui::BeginPopup("##AddPlanePopup"))
		{
			DrawFreePlaneAddItem(windowState, windowState.cursor3DPosition);
			ImGui::SeparatorText("Z zaznaczenia");
			DrawPlaneAddItem(windowState);
			ImGui::EndPopup();
		}

		if (toolButton("##ToolAddAtom", "tool-add-atom.png", "+", "Add atom", false))
		{
			RendererEvents::Viewport::AddAtomPopupToggleRequested event;
			event.windowId = windowState.windowId;
			const ImVec2 itemMinimum = ImGui::GetItemRectMin();
			const ImVec2 itemMaximum = ImGui::GetItemRectMax();
			event.screenPosition = glm::vec2(itemMaximum.x + 4.0f * uiScale, itemMinimum.y);
			eventBus->Publish(event);
		}

		const bool orbitalPressed =
			toolButton("##ToolAddOrbital", "tool-add-orbital.png", "Orb", "Add orbital - on every selected atom, or at the 3D cursor", false);
		const ImVec2 orbitalPopupPosition(
			ImGui::GetItemRectMax().x + 4.0f * uiScale, ImGui::GetItemRectMin().y);
		if (orbitalPressed)
			ImGui::OpenPopup("##AddOrbitalPopup");
		ImGui::SetNextWindowPos(orbitalPopupPosition, ImGuiCond_Appearing);
		if (ImGui::BeginPopup("##AddOrbitalPopup"))
		{
			DrawOrbitalAddMenu(windowState, windowState.cursor3DPosition, false);
			ImGui::EndPopup();
		}

		ImGui::EndChild();
		ImGui::PopStyleVar();
	}
} // namespace DefectStudio
