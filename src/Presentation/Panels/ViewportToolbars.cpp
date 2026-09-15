#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewportToolbars.hpp"


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
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"
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

	void DrawViewportToolbar(RendererWindowState &windowState, RendererLayer &layer)
	{
		const float uiScale = ImGui::GetIO().FontGlobalScale / kViewportToolbarFontScaleBaseline;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * uiScale, 4.0f * uiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 2.0f * uiScale));

		const float iconExtentBase = std::clamp(layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f);
		const float iconExtent = iconExtentBase * uiScale;
		const ImVec2 iconButtonSize(iconExtent, iconExtent);

		const float axisExtentBase = std::clamp(layer.GetGlobalSettings().viewport.axisButtonSize, 12.0f, 40.0f);
		const float axisExtent = axisExtentBase * uiScale;
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

		const float toolbarRowHeight = std::max(axisButtonSize.y, iconButtonSize.y) + 25.0f * uiScale;

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
			const RendererToolbarIconTexture *icon = layer.GetToolbarIcon(iconFileName);
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
			Ref<EventBus> eventBus = layer.GetEventBus();
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
			Ref<EventBus> eventBus = layer.GetEventBus();
			if (eventBus == nullptr)
				return;

			RendererEvents::Viewport::ZoomStepRequested event;
			event.windowId = windowState.windowId;
			event.amount = amount;
			eventBus->Publish(event);
		};

		// Like iconButton above but sized with axisButtonSize (its own configurable size, grouped
		// visually with these 6 buttons) instead of iconButtonSize - kept as its own lambda rather
		// than generalizing iconButton's signature, since that one's already called many times with
		// the implicit iconButtonSize capture.
		auto axisButton =
			[&](const char *id, const char *iconFileName, const char *fallback, const glm::vec3 &axis,
				const char *sourceAction, const char *tooltip)
		{
			bool pressed = false;
			const RendererToolbarIconTexture *icon = layer.GetToolbarIcon(iconFileName);
			if (icon != nullptr && icon->rendererId != 0)
			{
				const ImTextureRef textureRef(reinterpret_cast<void *>(static_cast<uintptr_t>(icon->rendererId)));
				pressed = ImGui::ImageButton(
					id, textureRef, axisButtonSize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
					ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			}
			else
			{
				pressed = ImGui::Button(fallback, axisButtonSize);
			}

			if (pressed)
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
		axisButton("##AxisA", "tool-axis-a.png", "a", lattice[0], "toolbar.align_axis_a", "Align to a-axis (1)");
		sameLineTight();

		axisButton("##AxisB", "tool-axis-b.png", "b", lattice[1], "toolbar.align_axis_b", "Align to b-axis (2)");
		sameLineTight();

		axisButton("##AxisC", "tool-axis-c.png", "c", lattice[2], "toolbar.align_axis_c", "Align to c-axis (3)");
		sameLineTight();

		axisButton(
			"##AxisAStar", "tool-axis-a-star.png", "a*", reciprocal[0], "toolbar.align_axis_a_star",
			"Align to a* (reciprocal) axis (Alt+1)");
		sameLineTight();

		axisButton(
			"##AxisBStar", "tool-axis-b-star.png", "b*", reciprocal[1], "toolbar.align_axis_b_star",
			"Align to b* (reciprocal) axis (Alt+2)");
		sameLineTight();

		axisButton(
			"##AxisCStar", "tool-axis-c-star.png", "c*", reciprocal[2], "toolbar.align_axis_c_star",
			"Align to c* (reciprocal) axis (Alt+3)");
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

		if (iconButton("##RollLeft", "rotate-left.png", "Rl-", "Roll left (Q, hold Alt for continuous)"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(+rotationDeltaRadians());
			queueTransition(animated, "toolbar.roll_left");
		}
		sameLineTight();

		if (iconButton("##RollRight", "rotate-right.png", "Rl+", "Roll right (E, hold Alt for continuous)"))
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
				layer.GetGlobalSettings().toolbarWheel.rotationStepDelta,
				layer.GetGlobalSettings().toolbarWheel.ctrlPresetValues,
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
			Ref<EventBus> eventBus = layer.GetEventBus();
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
				layer.GetGlobalSettings().toolbarWheel.zoomStepDelta,
				layer.GetGlobalSettings().toolbarWheel.ctrlPresetValues,
				0.0f,
				180.0f,
				windowState.percentStep);
		}
		sameLineTight();
		DrawViewportTransformControls(windowState, uiScale);

		ImGui::PopStyleVar(2);

		ImGui::Checkbox("Atoms", &windowState.showAtoms);
		ImGui::SameLine();
		ImGui::Checkbox("Bonds", &windowState.showBonds);
		ImGui::SameLine();
		ImGui::BeginDisabled(!windowState.showBonds);
		ImGui::Checkbox("Periodic", &windowState.showPeriodicBonds);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Bonds that cross the cell boundary into a periodic image.");
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
			layer.GetShowPeriodicTableWindow() = true;
		ImGui::SameLine();
		if (ImGui::Button("Export PNG..."))
		{
			try
			{
				RenderExportDialogState &dialog = layer.GetExportDialogState();
				dialog.open = true;
				dialog.targetWindowId = windowState.windowId;
				layer.PopulateExportPreviewState(dialog.previewState, windowState);

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
} // namespace DefectStudio
