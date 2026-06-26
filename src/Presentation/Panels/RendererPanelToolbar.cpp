#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/geometric.hpp>
#include <imgui.h>

#include "Core/Logging/Logger.hpp"
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

		float ComputeCameraTransitionDurationSeconds(float rotationSpeed)
		{
			const float safeSpeed = std::max(0.1f, rotationSpeed);
			return std::clamp(0.14f / safeSpeed, 0.02f, 0.50f);
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
			m_Layer.BeginViewInteraction(windowState.windowId, sourceAction);
			windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
				m_Layer.GetGlobalSettings().rotationSpeed);
			startCameraTransition(
				windowState,
				cameraState.Target(),
				cameraState.Distance(),
				cameraState.Yaw(),
				cameraState.Pitch(),
				cameraState.Roll(),
				sourceAction);
		};

		auto axisButton = [&](const char *label, const glm::vec3 &axis, const char *sourceAction)
		{
			if (ImGui::Button(label, axisButtonSize))
			{
				RendererViewCamera animated = *windowState.camera;
				animated.SetAlignToAxis(glm::normalize(axis), glm::vec3(0.0f, 0.0f, 1.0f));
				queueTransition(animated, sourceAction);
			}
		};

		const glm::mat3 &lattice = windowState.structure.lattice;
		const glm::mat3 &reciprocal = windowState.structure.reciprocalLattice;
		axisButton("a", lattice[0], "toolbar.align_axis_a");
		sameLineTight();

		axisButton("b", lattice[1], "toolbar.align_axis_b");
		sameLineTight();

		axisButton("c", lattice[2], "toolbar.align_axis_c");
		sameLineTight();

		axisButton("a*", reciprocal[0], "toolbar.align_axis_a_star");
		sameLineTight();

		axisButton("b*", reciprocal[1], "toolbar.align_axis_b_star");
		sameLineTight();

		axisButton("c*", reciprocal[2], "toolbar.align_axis_c_star");
		sameLineTight();

		if (iconButton("##OrbitUp", "rotate-arrow-z-in.png", "^", "Orbit up relative to camera"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, +orbitInputDelta());
			queueTransition(animated, "toolbar.orbit_up");
		}
		sameLineTight();

		if (iconButton("##OrbitDown", "rotate-arrow-z-out.png", "v", "Orbit down relative to camera"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, -orbitInputDelta());
			queueTransition(animated, "toolbar.orbit_down");
		}
		sameLineTight();

		if (iconButton("##OrbitLeft", "rotate-arrow-z-left.png", "<", "Orbit left relative to camera"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(+orbitInputDelta(), 0.0f);
			queueTransition(animated, "toolbar.orbit_left");
		}
		sameLineTight();

		if (iconButton("##OrbitRight", "rotate-arrow-z-right.png", ">", "Orbit right relative to camera"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(-orbitInputDelta(), 0.0f);
			queueTransition(animated, "toolbar.orbit_right");
		}
		sameLineTight();

		if (iconButton("##RollLeft", "rotate-left.png", "Rl-", "Roll left"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(+rotationDeltaRadians());
			queueTransition(animated, "toolbar.roll_left");
		}
		sameLineTight();

		if (iconButton("##RollRight", "rotate-right.png", "Rl+", "Roll right"))
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

		if (iconButton("##PanUp", "up-arrow.png", "P^", "Pan up"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(0.0f, -windowState.pixelStepPx);
			queueTransition(animated, "toolbar.pan_up");
		}

		sameLineTight();
		if (iconButton("##PanDown", "down-arrow.png", "Pv", "Pan down"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(0.0f, +windowState.pixelStepPx);
			queueTransition(animated, "toolbar.pan_down");
		}

		sameLineTight();
		if (iconButton("##PanLeft", "left-arrow.png", "P<", "Pan left"))
		{
			RendererViewCamera animated = *windowState.camera;
			animated.Pan(-windowState.pixelStepPx, 0.0f);
			queueTransition(animated, "toolbar.pan_left");
		}

		sameLineTight();
		if (iconButton("##PanRight", "right-arrow.png", "P>", "Pan right"))
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
		if (iconButton("##ZoomOut", "minus.png", "-", "Zoom out"))
		{
			m_Layer.BeginViewInteraction(windowState.windowId, "toolbar.zoom_out");
			windowState.transitionActive = false;
			windowState.camera->Zoom(-std::max(0.5f, windowState.percentStep * 0.1f));
			m_Layer.CommitViewInteraction(windowState.windowId);
		}
		sameLineTight();

		if (iconButton("##ZoomIn", "plus.png", "+", "Zoom in"))
		{
			m_Layer.BeginViewInteraction(windowState.windowId, "toolbar.zoom_in");
			windowState.transitionActive = false;
			windowState.camera->Zoom(+std::max(0.5f, windowState.percentStep * 0.1f));
			m_Layer.CommitViewInteraction(windowState.windowId);
		}
		sameLineTight();

		const bool isOrtho = windowState.camera->Projection() == CameraProjection::Orthographic;
		if (ImGui::Button(isOrtho ? "ORTHO" : "PERSP"))
		{
			m_Layer.BeginViewInteraction(windowState.windowId, "toolbar.toggle_projection");
			windowState.camera->ToggleProjection();
			m_Layer.CommitViewInteraction(windowState.windowId);
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

		ImGui::EndChild();
	}
} // namespace DefectStudio
