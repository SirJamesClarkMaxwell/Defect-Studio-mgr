#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <glm/geometric.hpp>
#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kOrbitMouseScale = 0.0065f;

		[[nodiscard]] float NextPresetValue(
			float currentValue,
			const std::vector<float> &presets,
			float minimumValue,
			float maximumValue,
			int direction)
		{
			if (presets.empty() || direction == 0)
				return std::clamp(currentValue, minimumValue, maximumValue);

			const float clampedCurrent = std::clamp(currentValue, minimumValue, maximumValue);
			if (direction > 0)
			{
				for (const float preset : presets)
				{
					if (preset > clampedCurrent + 0.0001f)
						return std::clamp(preset, minimumValue, maximumValue);
				}
				return std::clamp(presets.back(), minimumValue, maximumValue);
			}

			for (std::size_t index = presets.size(); index > 0; --index)
			{
				const float preset = presets[index - 1];
				if (preset < clampedCurrent - 0.0001f)
					return std::clamp(preset, minimumValue, maximumValue);
			}
			return std::clamp(presets.front(), minimumValue, maximumValue);
		}

		void ApplyToolbarWheelStep(
			float wheelDelta,
			bool usePresetList,
			float plainStepDelta,
			const std::vector<float> &presets,
			float minimumValue,
			float maximumValue,
			float &value)
		{
			if (wheelDelta == 0.0f)
				return;

			if (usePresetList)
			{
				const int direction = wheelDelta > 0.0f ? 1 : -1;
				value = NextPresetValue(value, presets, minimumValue, maximumValue, direction);
				return;
			}

			const float updated = value + wheelDelta * plainStepDelta;
			value = std::clamp(updated, minimumValue, maximumValue);
		}

	}

	void RendererPanel::drawViewportToolbar(RendererWindowState &windowState)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));

		const float iconExtent = std::clamp(m_Layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f);
		const ImVec2 iconButtonSize(iconExtent, iconExtent);

		const float axisExtent = std::clamp(m_Layer.GetGlobalSettings().viewport.axisButtonSize, 12.0f, 40.0f);
		const ImVec2 axisButtonSize(axisExtent, axisExtent);

		const auto rotationStepRadians = [&windowState]()
		{
			return std::clamp(windowState.rotationStepDeg, 0.0f, 180.0f) * 3.1415926535f / 180.0f;
		};
		const auto rotationDeltaRadians = [&rotationStepRadians]()
		{
			return rotationStepRadians();
		};
		const auto orbitInputDelta = [&rotationDeltaRadians]()
		{
			return rotationDeltaRadians() / kOrbitMouseScale;
		};

		const float toolbarRowHeight = std::max(axisButtonSize.y, iconButtonSize.y) + 25.0f;

		ImGui::BeginChild(
			"##ViewportToolbarRow",
			ImVec2(0.0f, toolbarRowHeight),
			false,
			ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		auto sameLineTight = []()
		{
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.70f);
		};

		auto iconButton = [&](const char *id, const char *iconFileName, const char *fallback, const char *tooltip) -> bool
		{
			bool pressed = false;
			const RendererToolbarIconTexture *icon = m_Layer.GetToolbarIcon(iconFileName);
			if (icon != nullptr && icon->rendererId != 0)
			{
				const ImTextureRef textureRef(reinterpret_cast<void *>(static_cast<uintptr_t>(icon->rendererId)));
				const ImVec2 uv0{0.0f, 0.0f}; // icon bottom left corner
				const ImVec2 uv1{1.0f, 1.0f}; // icon upper right corner
				const ImVec4 backgroundColor{0.0f, 0.0f, 0.0f, 0.0f};
				const ImVec4 tinColor{1.0f, 1.0f, 1.0f, 1.0f};
				pressed = ImGui::ImageButton(
					id, textureRef,
					iconButtonSize,
					uv0, uv1,
					backgroundColor, tinColor);
			}
			else
			{
				pressed = ImGui::Button(fallback, iconButtonSize);
			}

			if (tooltip != nullptr &&
				tooltip[0] != '\0' &&
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
			return pressed;
		};

		auto queueTransition = [&](const RendererViewCamera &cameraState, const char *sourceAction)
		{
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus == nullptr)
				return;

			RendererEvents::Viewport::ViewTransitionRequested event;
			event.windowId = windowState.windowId;
			event.targetView.target = cameraState.Target();
			event.targetView.distance = cameraState.Distance();
			event.targetView.yaw = cameraState.Yaw();
			event.targetView.pitch = cameraState.Pitch();
			event.targetView.roll = cameraState.Roll();
			event.targetView.projection = cameraState.Projection();
			event.sourceAction = sourceAction != nullptr ? sourceAction : "toolbar.view_transition";
			eventBus->Publish(event);
		};

		auto publishZoomStep = [&](float amount)
		{
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus == nullptr)
				return;

			RendererEvents::Viewport::ZoomStepRequested event;
			event.windowId = windowState.windowId;
			event.amount = amount;
			eventBus->Publish(event);
		};

		auto axisButton = [&](const char *label, const glm::vec3 &axis, const char *sourceAction, const char *tooltip)
		{
			if (ImGui::Button(label, axisButtonSize))
			{
				RendererViewCamera animated = *windowState.camera;
				animated.SetAlignToAxis(glm::normalize(axis), glm::vec3(0.0f, 0.0f, 1.0f));
				queueTransition(animated, sourceAction);
			}
			if (tooltip != nullptr && tooltip[0] != '\0' &&
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
		};

		if (ImGui::SmallButton("Rename"))
			ImGui::OpenPopup("##RendererWindowRenamePopup");
		if (ImGui::BeginPopup("##RendererWindowRenamePopup"))
		{
			char renameBuffer[256];
			std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", windowState.title.c_str());
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::InputText(
					"##RendererWindowRenameInput", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				if (renameBuffer[0] != '\0')
					windowState.title = renameBuffer;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		sameLineTight();

		const glm::mat3 &lattice = windowState.structure.lattice;
		const glm::mat3 &reciprocal = windowState.structure.reciprocalLattice;
		axisButton("a", lattice[0], "toolbar.align_axis_a", "Align to a-axis (1)");
		sameLineTight();

		axisButton("b", lattice[1], "toolbar.align_axis_b", "Align to b-axis (2)");
		sameLineTight();

		axisButton("c", lattice[2], "toolbar.align_axis_c", "Align to c-axis (3)");
		sameLineTight();

		axisButton("a*", reciprocal[0], "toolbar.align_axis_a_star", "Align to a* (reciprocal) axis (Alt+1)");
		sameLineTight();

		axisButton("b*", reciprocal[1], "toolbar.align_axis_b_star", "Align to b* (reciprocal) axis (Alt+2)");
		sameLineTight();

		axisButton("c*", reciprocal[2], "toolbar.align_axis_c_star", "Align to c* (reciprocal) axis (Alt+3)");
		sameLineTight();

		if (iconButton("##OrbitUp", "rotate-arrow-z-in.png", "^", "Orbit up relative to camera (Up, hold Alt for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, +orbitInputDelta());
			queueTransition(animated, "toolbar.orbit_up");
		}
		sameLineTight();

		if (iconButton("##OrbitDown", "rotate-arrow-z-out.png", "v", "Orbit down relative to camera (Down, hold Alt for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, -orbitInputDelta());
			queueTransition(animated, "toolbar.orbit_down");
		}
		sameLineTight();

		if (iconButton("##OrbitLeft", "rotate-arrow-z-left.png", "<", "Orbit left relative to camera (Left, hold Alt for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(+orbitInputDelta(), 0.0f);
			queueTransition(animated, "toolbar.orbit_left");
		}
		sameLineTight();

		if (iconButton("##OrbitRight", "rotate-arrow-z-right.png", ">", "Orbit right relative to camera (Right, hold Alt for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(-orbitInputDelta(), 0.0f);
			queueTransition(animated, "toolbar.orbit_right");
		}
		sameLineTight();

		if (iconButton("##RollLeft", "rotate-left.png", "Rl-", "Roll left (Q)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(+rotationDeltaRadians());
			queueTransition(animated, "toolbar.roll_left");
		}
		sameLineTight();

		if (iconButton("##RollRight", "rotate-right.png", "Rl+", "Roll right (E)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(-rotationDeltaRadians());
			queueTransition(animated, "toolbar.roll_right");
		}
		sameLineTight();

		// sameLineTight();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputFloat("##step_deg", &windowState.rotationStepDeg, 0.0f, 0.0f, "%.1f");
		windowState.rotationStepDeg = std::clamp(windowState.rotationStepDeg, 0.0f, 180.0f);
		const bool rotationStepHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGuiIO &io = ImGui::GetIO();
		if (rotationStepHovered)
		{
			ApplyToolbarWheelStep(
				io.MouseWheel,
				io.KeyCtrl,
				m_Layer.GetGlobalSettings().toolbarWheel.rotationStepDelta,
				m_Layer.GetGlobalSettings().toolbarWheel.ctrlPresetValues,
				0.0f,
				180.0f,
				windowState.rotationStepDeg);
		}
		sameLineTight();

		if (iconButton("##PanUp", "up-arrow.png", "P^", "Pan up (Shift+Up, hold Alt+Shift for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(0.0f, -windowState.pixelStepPx);
			queueTransition(animated, "toolbar.pan_up");
		}

		sameLineTight();
		if (iconButton("##PanDown", "down-arrow.png", "Pv", "Pan down (Shift+Down, hold Alt+Shift for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(0.0f, +windowState.pixelStepPx);
			queueTransition(animated, "toolbar.pan_down");
		}

		sameLineTight();
		if (iconButton("##PanLeft", "left-arrow.png", "P<", "Pan left (Shift+Left, hold Alt+Shift for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(-windowState.pixelStepPx, 0.0f);
			queueTransition(animated, "toolbar.pan_left");
		}

		sameLineTight();
		if (iconButton("##PanRight", "right-arrow.png", "P>", "Pan right (Shift+Right, hold Alt+Shift for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(+windowState.pixelStepPx, 0.0f);
			queueTransition(animated, "toolbar.pan_right");
		}

		sameLineTight();

		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputFloat("##step_px", &windowState.pixelStepPx, 0.0f, 0.0f, "%.0f");
		windowState.pixelStepPx = std::clamp(windowState.pixelStepPx, 1.0f, 512.0f);

		sameLineTight();
		if (iconButton("##ZoomOut", "minus.png", "-", "Zoom out (-)"))
		{
			publishZoomStep(-std::max(0.5f, windowState.percentStep * 0.1f));
		}
		sameLineTight();

		if (iconButton("##ZoomIn", "plus.png", "+", "Zoom in (+)"))
		{
			publishZoomStep(+std::max(0.5f, windowState.percentStep * 0.1f));
		}
		sameLineTight();

		const bool isOrtho = windowState.camera->Projection() == CameraProjection::Orthographic;
		if (ImGui::Button(isOrtho ? "ORTHO" : "PERSP"))
		{
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::ProjectionToggleRequested event;
				event.windowId = windowState.windowId;
				eventBus->Publish(event);
			}
		}
		sameLineTight();

		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputFloat("##step_pct", &windowState.percentStep, 0.0f, 0.0f, "%.0f");
		windowState.percentStep = std::clamp(windowState.percentStep, 0.0f, 180.0f);
		const bool zoomStepHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		if (zoomStepHovered)
		{
			ApplyToolbarWheelStep(
				io.MouseWheel,
				io.KeyCtrl,
				m_Layer.GetGlobalSettings().toolbarWheel.zoomStepDelta,
				m_Layer.GetGlobalSettings().toolbarWheel.ctrlPresetValues,
				0.0f,
				180.0f,
				windowState.percentStep);
		}
		sameLineTight();

		ImGui::PopStyleVar(2);

		ImGui::Checkbox("Atoms", &windowState.showAtoms);
		ImGui::SameLine();
		ImGui::Checkbox("Bonds", &windowState.showBonds);
		ImGui::SameLine();
		ImGui::Checkbox("Cell", &windowState.showCellBox);
		ImGui::SameLine();
		ImGui::Checkbox("Grid", &windowState.showGrid);
		ImGui::SameLine();
		if (ImGui::Button("Reset View"))
		{
			RendererViewCamera animated = *windowState.camera;
			glm::vec3 minimum(1e6f, 1e6f, 1e6f);
			glm::vec3 maximum(-1e6f, -1e6f, -1e6f);
			for (const RendererAtomData &atom : windowState.structure.atoms)
			{
				minimum = glm::min(minimum, atom.cartesianPosition);
				maximum = glm::max(maximum, atom.cartesianPosition);
			}
			animated.FocusBounds(minimum, maximum);
			animated.SetFromDirection(glm::normalize(glm::vec3(1.0f, 1.0f, 0.9f)));
			queueTransition(animated, "toolbar.reset_view");
		}
		ImGui::SameLine();
		if (ImGui::Button("Periodic Table"))
			m_Layer.GetShowPeriodicTableWindow() = true;
		ImGui::SameLine();
		if (ImGui::Button("Export PNG..."))
		{
			try
			{
				RenderExportDialogState &dialog = m_Layer.GetExportDialogState();
				dialog.open = true;
				dialog.targetWindowId = windowState.windowId;
				dialog.previewState.camera = CreateUnique<RendererViewCamera>(*windowState.camera);
				dialog.previewState.showAtoms = windowState.showAtoms;
				dialog.previewState.showBonds = windowState.showBonds;
				dialog.previewState.showCellBox = windowState.showCellBox;
				dialog.previewState.showGrid = windowState.showGrid;
				dialog.previewState.selectedAtomIndices = windowState.selectedAtomIndices;
				// See RendererLayer::onExportImageRequested's matching comment - previewState is a
				// fresh RendererWindowState, so pinned bond/angle labels need an explicit copy or
				// they silently never appear in the export.
				dialog.previewState.pinnedMeasurements = windowState.pinnedMeasurements;
				dialog.previewState.bondLabelsAlignToDirection = windowState.bondLabelsAlignToDirection;

				const std::string stem = windowState.structure.sourcePath.Native().stem().string();
				dialog.filename = (stem.empty() ? "structure" : stem) + "_export";
				// drawExportDialog() calls ImGui::OpenPopup once it sees dialog.open - keeps the
				// button and the F12 command path (RendererLayer::onExportImageRequested, which
				// runs outside any ImGui window context and can't call OpenPopup itself) in sync.
			}
			catch (const std::exception &exception)
			{
				DS_LOG_ERROR("Export dialog open failed: {}", exception.what());
			}
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Export viewport as PNG (F12)");

		ImGui::EndChild();
	}

	// VESTA-style vertical icon strip along the viewport's left edge: one click each for the tool
	// modes that would otherwise only be reachable via keyboard shortcut (G/R/S/B/C/M/Shift+M) or
	// not at all (3D cursor placement, "nothing"/idle tool). Publishes the same events those
	// shortcuts do rather than going through CommandRegistry, matching this file's existing style
	// (see drawViewportToolbar's iconButton/queueTransition above) - none of these need undo or a
	// command-palette entry of their own beyond what's already registered for the keybindings.
	void RendererPanel::drawViewportVerticalToolbar(RendererWindowState &windowState)
	{
		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return;

		const float iconExtent = std::clamp(m_Layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f);
		const ImVec2 buttonSize(iconExtent, iconExtent);
		const float columnWidth = iconExtent + ImGui::GetStyle().WindowPadding.x * 2.0f + 4.0f;

		ImGui::BeginChild(
			"##ViewportVerticalToolbar", ImVec2(columnWidth, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_NoScrollbar);

		auto toolButton = [&](const char *id, const char *iconFileName, const char *fallback, const char *tooltip, bool active) -> bool
		{
			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

			bool pressed = false;
			const RendererToolbarIconTexture *icon = m_Layer.GetToolbarIcon(iconFileName);
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

		ImGui::EndChild();
	}

	// "Use 3D Cursor"/"Use Selection Center" below always fill a CARTESIAN position and force
	// Cartesian mode - simplest correct behavior without needing a fractional<->cartesian
	// conversion here (RendererStructureData carries the lattice matrices but not the domain
	// CrystalStructure::FractionalToCartesian helper); switch back to Fractional afterwards and
	// retype if that's genuinely what's needed.
	// Was a BeginPopupModal - modal semantics dim and block input to the ENTIRE app, including the
	// separate Periodic Table window this popup opens via "Choose..." below, making that button
	// non-functional (couldn't click any element while the modal held focus). A plain window behaves
	// like the Periodic Table window itself: both stay open and interactive side by side.
	void RendererPanel::drawAddAtomPopup()
	{
		constexpr const char *kPopupId = "Add Atom";
		if (!m_AddAtomPopupRequested)
			return;

		ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(kPopupId, &m_AddAtomPopupRequested, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		RendererWindowState *windowState = nullptr;
		for (RendererWindowState &candidate : m_Layer.GetWindows())
		{
			if (candidate.windowId == m_AddAtomPopupWindowId)
			{
				windowState = &candidate;
				break;
			}
		}
		if (windowState == nullptr)
		{
			ImGui::TextDisabled("Target window is no longer open.");
			if (ImGui::Button("Close"))
				m_AddAtomPopupRequested = false;
			ImGui::End();
			return;
		}

		// Reuses the Periodic Table window's own selection (RendererLayer::GetSelectedPeriodicElement)
		// instead of embedding a second grid here - the app already has a dedicated element picker,
		// no need for two.
		if (m_Layer.GetSelectedPeriodicElement().empty())
			m_Layer.GetSelectedPeriodicElement() = "C";
		ImGui::Text("Element: %s", m_Layer.GetSelectedPeriodicElement().c_str());
		ImGui::SameLine();
		if (ImGui::Button("Choose..."))
			m_Layer.GetShowPeriodicTableWindow() = true;

		if (ImGui::RadioButton("Cartesian", !m_AddAtomPopupFractional))
			m_AddAtomPopupFractional = false;
		ImGui::SameLine();
		if (ImGui::RadioButton("Fractional", m_AddAtomPopupFractional))
			m_AddAtomPopupFractional = true;

		ImGui::InputFloat3("Position", &m_AddAtomPopupPosition.x, "%.4f");

		ImGui::BeginDisabled(!windowState->cursor3DPlaced);
		if (ImGui::Button("Use 3D Cursor"))
		{
			m_AddAtomPopupPosition = windowState->cursor3DPosition;
			m_AddAtomPopupFractional = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(windowState->selectedAtomIndices.empty());
		if (ImGui::Button("Use Selection Center"))
		{
			glm::vec3 centroid(0.0f);
			for (const std::size_t atomIndex : windowState->selectedAtomIndices)
				centroid += windowState->structure.atoms[atomIndex].cartesianPosition;
			centroid /= static_cast<float>(windowState->selectedAtomIndices.size());
			m_AddAtomPopupPosition = centroid;
			m_AddAtomPopupFractional = false;
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		const bool canInsert = !m_Layer.GetSelectedPeriodicElement().empty();
		ImGui::BeginDisabled(!canInsert);
		if (ImGui::Button("Insert"))
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				AddAtomAtCoordinatesPayload payload;
				payload.windowId = windowState->windowId;
				payload.species = m_Layer.GetSelectedPeriodicElement();
				payload.position = m_AddAtomPopupPosition;
				payload.isFractional = m_AddAtomPopupFractional;

				CommandContext context;
				context.Set<AddAtomAtCoordinatesPayload>("atom_edit.add_atom_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.atoms.add_at_coordinates"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Add atom failed: {}", result.Error().technicalDetails);
				else
					m_AddAtomPopupRequested = false;
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			m_AddAtomPopupRequested = false;

		ImGui::End();
	}
} // namespace DefectStudio
