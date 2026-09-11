#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewportToolbars.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
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

		const float iconExtent = std::clamp(layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f);
		const ImVec2 buttonSize(iconExtent, iconExtent);
		const float columnWidth = iconExtent + ImGui::GetStyle().WindowPadding.x * 2.0f ;//+ 4.0f;

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

		// Rotate-only pivot toggle for the SceneArrow gizmo (RendererPanel::renderSceneArrowTransformGizmo)
		// - meaningless for Translate/Scale (thickness-only, no pivot concept) or for the atom/label
		// gizmos, so only shown when it would actually do something. Pure toolbar-local UI state (like
		// rotationStepDeg/pixelStepPx/percentStep above), mutated directly rather than through a
		// GizmoOperationRequested-style event - nothing else (no keybinding) needs to reach it. No icon
		// asset - "tool-pivot-cursor.png" doesn't exist, so toolButton's plain-text fallback always
		// renders, same as the "1"/"2"/"3"/"4" selection-mode buttons above.
		using ArrowGizmoPivotMode = RendererWindowState::ArrowGizmoPivotMode;
		if (!windowState.selectedSceneArrows.empty() && windowState.gizmoOperation == GizmoOperation::Rotate)
		{
			const bool cursorPivot = windowState.sceneArrowGizmoPivotMode == ArrowGizmoPivotMode::Cursor3D;
			if (toolButton(
					"##ToolArrowPivot", "tool-pivot-cursor.png", cursorPivot ? "Csr" : "Mid",
					cursorPivot ? "Arrow rotate pivot: 3D Cursor (click for Midpoint)"
								: "Arrow rotate pivot: Midpoint (click for 3D Cursor)",
					cursorPivot))
			{
				windowState.sceneArrowGizmoPivotMode =
					cursorPivot ? ArrowGizmoPivotMode::Midpoint : ArrowGizmoPivotMode::Cursor3D;
			}
		}

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

		if (toolButton(
				"##ModeAtoms", "tool-mode-atoms.png", "1", "Selection mode: Atoms only (Ctrl+1)",
				windowState.pickAtoms && !windowState.pickBonds && !windowState.pickLabels))
			publishSelectionMode(true, false, false);
		if (toolButton(
				"##ModeAtomsBonds", "tool-mode-atoms-bonds.png", "2", "Selection mode: Atoms + Bonds (Ctrl+2)",
				windowState.pickAtoms && windowState.pickBonds && !windowState.pickLabels))
			publishSelectionMode(true, true, false);
		if (toolButton(
				"##ModeBondsLabels", "tool-mode-bonds-labels.png", "3", "Selection mode: Bonds + Labels, no atoms (Ctrl+3)",
				!windowState.pickAtoms && windowState.pickBonds && windowState.pickLabels))
			publishSelectionMode(false, true, true);
		if (toolButton(
				"##ModeAll", "tool-mode-all.png", "4", "Selection mode: Atoms + Bonds + Labels (Ctrl+4)",
				windowState.pickAtoms && windowState.pickBonds && windowState.pickLabels))
			publishSelectionMode(true, true, true);
		if (toolButton(
				"##ModeLabelsOnly", "tool-mode-labels.png", "5",
				"Selection mode: Pinned labels only, no atoms/bonds - for multi-selecting bond/angle "
				"labels to edit their style together (Ctrl+5)",
				!windowState.pickAtoms && !windowState.pickBonds && windowState.pickLabels))
			publishSelectionMode(false, false, true);

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

		ImGui::EndChild();
	}
} // namespace DefectStudio
