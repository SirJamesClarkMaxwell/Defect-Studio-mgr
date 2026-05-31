#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <imgui.h>

#include "Core/Utils/Logger.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kViewportMinSize = 64.0f;
		constexpr float kViewportMaxSize = 8192.0f;
		constexpr float kOrbitMouseScale = 0.0065f;

		using PeriodicTableRow = std::array<const char *, 18>;
		const std::array<PeriodicTableRow, 7> kPeriodicTableRows = {
			PeriodicTableRow{"H", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "He"},
			PeriodicTableRow{"Li", "Be", "", "", "", "", "", "", "", "", "", "", "B", "C", "N", "O", "F", "Ne"},
			PeriodicTableRow{"Na", "Mg", "", "", "", "", "", "", "", "", "", "", "Al", "Si", "P", "S", "Cl", "Ar"},
			PeriodicTableRow{"K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr"},
			PeriodicTableRow{"Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe"},
			PeriodicTableRow{"Cs", "Ba", "", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn"},
			PeriodicTableRow{"Fr", "Ra", "", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "", "", "", "", "", "", ""}};

		const std::array<const char *, 15> kLanthanides = {
			"La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu"};
		const std::array<const char *, 15> kActinides = {
			"Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr"};

		[[nodiscard]] float NormalizeAngleRadians(float angle)
		{
			constexpr float kTwoPi = 6.283185307f;
			constexpr float kPi = 3.1415926535f;
			while (angle > kPi)
				angle -= kTwoPi;
			while (angle < -kPi)
				angle += kTwoPi;
			return angle;
		}

		[[nodiscard]] float LerpAngleRadians(float from, float to, float t)
		{
			return from + NormalizeAngleRadians(to - from) * t;
		}

		[[nodiscard]] float EaseOutCubic(float t)
		{
			const float clamped = std::clamp(t, 0.0f, 1.0f);
			const float inv = 1.0f - clamped;
			return 1.0f - inv * inv * inv;
		}

		[[nodiscard]] float RadiansToDegrees(float angleRadians)
		{
			return angleRadians * 57.295779513f;
		}

		[[nodiscard]] float SanitizeViewportDimension(float value)
		{
			if (!std::isfinite(value))
				return 640.0f;
			return std::clamp(value, kViewportMinSize, kViewportMaxSize);
		}

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

		void LogRotationAction(
			const char *actionName,
			float stepDegrees,
			float rotationSpeed,
			float deltaRadians,
			float orbitInputDelta)
		{
			DS_LOG_INFO(
				"Renderer rotation action={} step_deg={:.3f} speed={:.3f} delta_rad={:.6f} orbit_input_delta={:.3f}",
				actionName,
				stepDegrees,
				rotationSpeed,
				deltaRadians,
				orbitInputDelta);
		}

		float ComputeCameraTransitionDurationSeconds(float rotationSpeed)
		{
			const float safeSpeed = std::max(0.1f, rotationSpeed);
			return std::clamp(0.14f / safeSpeed, 0.02f, 0.50f);
		}

	}

	RendererPanel::RendererPanel(RendererLayer &layer)
		: m_Layer(layer)
	{
	}

	void RendererPanel::Render(float deltaTime)
	{
		if (!m_Layer.m_Attached || m_Layer.m_RendererBackend == nullptr)
			return;

		for (RendererWindowState &windowState : m_Layer.m_Windows)
			renderStructureWindow(windowState, deltaTime);

		// drawPeriodicTableWindow();
		m_Layer.m_RendererBackend->CollectProfilingData();
	}

	void RendererPanel::renderStructureWindow(RendererWindowState &windowState, float deltaTime)
	{
		if (windowState.camera == nullptr)
			return;

		updateCameraTransition(windowState, deltaTime);

		const bool began = ImGui::Begin(windowState.title.c_str());
		if (!began)
		{
			ImGui::End();
			return;
		}

		drawViewportToolbar(windowState);
		ImGui::Separator();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		windowState.viewportSize.x = SanitizeViewportDimension(available.x);
		windowState.viewportSize.y = SanitizeViewportDimension(available.y);
		windowState.camera->SetViewport(windowState.viewportSize.x, windowState.viewportSize.y);

		const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();

		const unsigned int textureId = m_Layer.m_RendererBackend->RenderWindow(
			windowState.title,
			windowState.structure,
			*windowState.camera,
			m_Layer.m_GlobalRenderSettings,
			static_cast<int>(windowState.viewportSize.x),
			static_cast<int>(windowState.viewportSize.y),
			windowState.showAtoms,
			windowState.showBonds,
			windowState.showCellBox,
			windowState.showGrid,
			windowState.selectedAtomIndices);

		ImGui::Image(
			static_cast<ImTextureID>(static_cast<uintptr_t>(textureId)),
			windowState.viewportSize,
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));

		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			applyViewportInputNavigation(windowState, imageOrigin, deltaTime);

			ImGuiIO &io = ImGui::GetIO();
			const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				!ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
				!io.KeyAlt;
			if (leftClicked)
			{
				const ImVec2 mousePos = ImGui::GetMousePos();
				const float relX = mousePos.x - imageOrigin.x;
				const float relY = mousePos.y - imageOrigin.y;
				if (relX >= 0.0f &&
					relY >= 0.0f &&
					relX < windowState.viewportSize.x &&
					relY < windowState.viewportSize.y)
				{
					handleAtomPick(windowState, relX, relY, io.KeyCtrl);
				}
			}
		}
		else
		{
			windowState.dragActive = false;
		}

		applyViewportKeyboardNavigation(windowState);

		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
	}

	void RendererPanel::drawViewportToolbar(RendererWindowState &windowState)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));

		const float iconExtent = std::clamp(m_Layer.m_GlobalRenderSettings.viewport.iconButtonSize, 12.0f, 40.0f);
		const ImVec2 iconButtonSize(iconExtent, iconExtent);

		const float axisExtent = std::clamp(m_Layer.m_GlobalRenderSettings.viewport.axisButtonSize, 12.0f, 40.0f);
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
			const RendererToolbarIconTexture *icon = m_Layer.getToolbarIcon(iconFileName);
			if (icon != nullptr && icon->rendererId != 0)
			{
				const ImTextureRef textureRef(reinterpret_cast<void *>(static_cast<uintptr_t>(icon->rendererId)));
				pressed = ImGui::ImageButton(
					id,
					textureRef,
					iconButtonSize,
					ImVec2(0.0f, 0.0f),
					ImVec2(1.0f, 1.0f),
					ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
					ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
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
			windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
				m_Layer.m_GlobalRenderSettings.rotationSpeed);
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
			LogRotationAction(
				"toolbar.orbit_up",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians(),
				orbitInputDelta());
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, +orbitInputDelta());
			queueTransition(animated, "toolbar.orbit_up");
		}
		sameLineTight();
		
		if (iconButton("##OrbitDown", "rotate-arrow-z-out.png", "v", "Orbit down relative to camera"))
		{
			LogRotationAction(
				"toolbar.orbit_down",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians(),
				orbitInputDelta());
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, -orbitInputDelta());
			queueTransition(animated, "toolbar.orbit_down");
		}
		sameLineTight();
		
		if (iconButton("##OrbitLeft", "rotate-arrow-z-left.png", "<", "Orbit left relative to camera"))
		{
			LogRotationAction(
				"toolbar.orbit_left",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians(),
				orbitInputDelta());
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(+orbitInputDelta(), 0.0f);
			queueTransition(animated, "toolbar.orbit_left");
		}
		sameLineTight();
		
		if (iconButton("##OrbitRight", "rotate-arrow-z-right.png", ">", "Orbit right relative to camera"))
		{
			LogRotationAction(
				"toolbar.orbit_right",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians(),
				orbitInputDelta());
				RendererViewCamera animated = *windowState.camera;
				animated.Orbit(-orbitInputDelta(), 0.0f);
				queueTransition(animated, "toolbar.orbit_right");
		}
		sameLineTight();
		
		if (iconButton("##RollLeft", "rotate-left.png", "Rl-", "Roll left"))
		{
			LogRotationAction(
				"toolbar.roll_left",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians(),
				orbitInputDelta());
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(+rotationDeltaRadians());
			queueTransition(animated, "toolbar.roll_left");
		}
		sameLineTight();
		
		if (iconButton("##RollRight", "rotate-right.png", "Rl+", "Roll right"))
		{
			LogRotationAction(
				"toolbar.roll_right",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians(),
				orbitInputDelta());
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
				m_Layer.m_GlobalRenderSettings.toolbarWheel.rotationStepDelta,
				m_Layer.m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues,
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
			windowState.transitionActive = false;
			windowState.camera->Zoom(-std::max(0.5f, windowState.percentStep * 0.1f));
		}
		sameLineTight();

		if (iconButton("##ZoomIn", "plus.png", "+", "Zoom in"))
		{
			windowState.transitionActive = false;
			windowState.camera->Zoom(+std::max(0.5f, windowState.percentStep * 0.1f));
		}
		sameLineTight();

		const bool isOrtho = windowState.camera->Projection() == CameraProjection::Orthographic;
		if (ImGui::Button(isOrtho ? "ORTHO" : "PERSP"))
			windowState.camera->ToggleProjection();
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
				m_Layer.m_GlobalRenderSettings.toolbarWheel.zoomStepDelta,
				m_Layer.m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues,
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
			m_Layer.m_ShowPeriodicTableWindow = true;

		ImGui::EndChild();
	}

	

	void RendererPanel::applyViewportKeyboardNavigation(RendererWindowState &windowState)
	{
		if (windowState.camera == nullptr)
			return;

		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;

		ImGuiIO &io = ImGui::GetIO();
		if (io.WantTextInput || ImGui::IsAnyItemActive())
			return;

		const auto queueTransition = [&](const RendererViewCamera &cameraState, const char *sourceAction)
		{
			windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
				m_Layer.m_GlobalRenderSettings.rotationSpeed);
			startCameraTransition(
				windowState,
				cameraState.Target(),
				cameraState.Distance(),
				cameraState.Yaw(),
				cameraState.Pitch(),
				cameraState.Roll(),
				sourceAction);
		};

		const auto alignToAxis = [&](const glm::vec3 &axis)
		{
			if (glm::dot(axis, axis) <= 1e-8f)
				return;

			RendererViewCamera animated = *windowState.camera;
			animated.SetAlignToAxis(glm::normalize(axis), glm::vec3(0.0f, 0.0f, 1.0f));
			queueTransition(animated, "keyboard.align_axis");
		};

		const auto focusSelectedAtom = [&]()
		{
			if (windowState.selectedAtomIndices.empty())
				return;

			const std::size_t selectedIndex = windowState.selectedAtomIndices.back();
			if (selectedIndex >= windowState.structure.atoms.size())
				return;

			const RendererAtomData &atom = windowState.structure.atoms[selectedIndex];
			float desiredDistance = m_Layer.m_GlobalRenderSettings.focusSelectedAtomDistance;
			if (m_Layer.m_GlobalRenderSettings.focusSelectedAtomRespectAtomRadius)
			{
				const float radiusDistance = atom.radius * m_Layer.m_GlobalRenderSettings.focusSelectedAtomRadiusMultiplier;
				desiredDistance = std::max(desiredDistance, radiusDistance);
			}
			windowState.transitionDuration = std::max(
				0.02f,
				m_Layer.m_GlobalRenderSettings.focusSelectedAtomTransitionSeconds);

			startCameraTransition(
				windowState,
				atom.cartesianPosition,
				desiredDistance,
				windowState.camera->Yaw(),
				windowState.camera->Pitch(),
				windowState.camera->Roll(),
				"keyboard.focus_selected_atom");
		};

		const float rotationStepRadians = std::clamp(windowState.rotationStepDeg, 0.0f, 180.0f) * 3.1415926535f / 180.0f;
		const float rotationDeltaRadians = rotationStepRadians;
		const float orbitInputDelta = rotationDeltaRadians / kOrbitMouseScale;
		const RendererKeyboardShortcutSettings &shortcuts = m_Layer.m_GlobalRenderSettings.shortcuts;

		const auto shortcutPressed = [](ImGuiKey key) -> bool
		{
			return key != ImGuiKey_None && ImGui::IsKeyPressed(key);
		};

		const glm::mat3 &lattice = windowState.structure.lattice;
		if (shortcutPressed(shortcuts.alignAxisA))
			alignToAxis(lattice[0]);
		if (shortcutPressed(shortcuts.alignAxisB))
			alignToAxis(lattice[1]);
		if (shortcutPressed(shortcuts.alignAxisC))
			alignToAxis(lattice[2]);

		if (shortcutPressed(shortcuts.orbitLeft))
		{
			LogRotationAction(
				"keyboard.orbit_left",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(+orbitInputDelta, 0.0f);
			queueTransition(animated, "keyboard.orbit_left");
		}
		if (shortcutPressed(shortcuts.orbitRight))
		{
			LogRotationAction(
				"keyboard.orbit_right",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(-orbitInputDelta, 0.0f);
			queueTransition(animated, "keyboard.orbit_right");
		}
		if (shortcutPressed(shortcuts.orbitUp))
		{
			LogRotationAction(
				"keyboard.orbit_up",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, +orbitInputDelta);
			queueTransition(animated, "keyboard.orbit_up");
		}
		if (shortcutPressed(shortcuts.orbitDown))
		{
			LogRotationAction(
				"keyboard.orbit_down",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, -orbitInputDelta);
			queueTransition(animated, "keyboard.orbit_down");
		}
		if (shortcutPressed(shortcuts.rollLeft))
		{
			LogRotationAction(
				"keyboard.roll_left",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(+rotationDeltaRadians);
			queueTransition(animated, "keyboard.roll_left");
		}
		if (shortcutPressed(shortcuts.rollRight))
		{
			LogRotationAction(
				"keyboard.roll_right",
				windowState.rotationStepDeg,
				m_Layer.m_GlobalRenderSettings.rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(-rotationDeltaRadians);
			queueTransition(animated, "keyboard.roll_right");
		}

		if (shortcutPressed(shortcuts.zoomIn))
		{
			windowState.transitionActive = false;
			windowState.camera->Zoom(+std::max(0.5f, windowState.percentStep * 0.1f));
		}
		if (shortcutPressed(shortcuts.zoomOut))
		{
			windowState.transitionActive = false;
			windowState.camera->Zoom(-std::max(0.5f, windowState.percentStep * 0.1f));
		}

		if (shortcutPressed(shortcuts.focusSelectedAtom))
			focusSelectedAtom();
	}

	void RendererPanel::applyViewportInputNavigation(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		float deltaTime)
	{
		(void)imageOrigin;
		if (windowState.camera == nullptr)
			return;

		ImGuiIO &io = ImGui::GetIO();
		const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool altPressed = io.KeyAlt;
		const bool shiftPressed = io.KeyShift;

		const bool touchpadOrbit = m_Layer.m_GlobalRenderSettings.touchpadNavigation && altPressed && lmb;
		const bool touchpadPan = m_Layer.m_GlobalRenderSettings.touchpadNavigation && altPressed && shiftPressed && lmb;
		const bool touchpadZoom = m_Layer.m_GlobalRenderSettings.touchpadNavigation && altPressed && rmb;
		const bool dragActiveInput = mmb || touchpadOrbit || touchpadPan || touchpadZoom;
		if (dragActiveInput)
			windowState.transitionActive = false;

		if (!dragActiveInput)
		{
			windowState.dragActive = false;
			windowState.lastMousePosition = io.MousePos;
		}

		float wheel = io.MouseWheel;
		if (m_Layer.m_GlobalRenderSettings.invertZoom)
			wheel = -wheel;
		if (wheel != 0.0f)
		{
			windowState.transitionActive = false;
			windowState.camera->Zoom(wheel * m_Layer.m_GlobalRenderSettings.zoomSensitivity);
		}

		if (!dragActiveInput)
			return;

		if (!windowState.dragActive)
		{
			windowState.dragActive = true;
			windowState.lastMousePosition = io.MousePos;
			return;
		}

		ImVec2 delta(
			io.MousePos.x - windowState.lastMousePosition.x,
			io.MousePos.y - windowState.lastMousePosition.y);
		windowState.lastMousePosition = io.MousePos;
		const float frameScale = std::max(0.0f, deltaTime) * 60.0f;
		delta.x *= frameScale;
		delta.y *= frameScale;

		if (touchpadZoom)
		{
			windowState.camera->Zoom(
				(-delta.y * 0.020f) * m_Layer.m_GlobalRenderSettings.zoomSensitivity);
			return;
		}

		if ((mmb && shiftPressed) || touchpadPan)
		{
			windowState.camera->Pan(
				delta.x * m_Layer.m_GlobalRenderSettings.panSensitivity,
				delta.y * m_Layer.m_GlobalRenderSettings.panSensitivity);
			return;
		}

		windowState.camera->Orbit(
			delta.x * m_Layer.m_GlobalRenderSettings.orbitSensitivity,
			delta.y * m_Layer.m_GlobalRenderSettings.orbitSensitivity);
	}

	void RendererPanel::handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive)
	{
		if (!windowState.camera || windowState.structure.atoms.empty())
			return;

		const float vpW = windowState.viewportSize.x;
		const float vpH = windowState.viewportSize.y;
		if (vpW <= 0.0f || vpH <= 0.0f)
			return;

		const float ndcX = (2.0f * relX / vpW) - 1.0f;
		const float ndcY = -((2.0f * relY / vpH) - 1.0f);

		const glm::mat4 invVP = glm::inverse(
			windowState.camera->ProjectionMatrix() *
			windowState.camera->ViewMatrix());

		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

		float bestT = std::numeric_limits<float>::max();
		std::size_t hitIndex = std::numeric_limits<std::size_t>::max();

		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			const RendererAtomData &atom = windowState.structure.atoms[i];
			const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
			const float a = glm::dot(rayDir, rayDir);
			const float b = 2.0f * glm::dot(oc, rayDir);
			const float c = glm::dot(oc, oc) - atom.radius * atom.radius;
			const float disc = b * b - 4.0f * a * c;
			if (disc < 0.0f)
				continue;
			const float t = (-b - std::sqrt(disc)) / (2.0f * a);
			if (t > 0.001f && t < bestT)
			{
				bestT = t;
				hitIndex = i;
			}
		}

		if (hitIndex == std::numeric_limits<std::size_t>::max())
		{
			if (!additive)
				windowState.selectedAtomIndices.clear();
			return;
		}

		if (!additive)
		{
			windowState.selectedAtomIndices.clear();
			windowState.selectedAtomIndices.push_back(hitIndex);
			return;
		}

		auto it = std::find(
			windowState.selectedAtomIndices.begin(),
			windowState.selectedAtomIndices.end(),
			hitIndex);
		if (it != windowState.selectedAtomIndices.end())
			windowState.selectedAtomIndices.erase(it);
		else
			windowState.selectedAtomIndices.push_back(hitIndex);
	}

	void RendererPanel::drawPeriodicTableWindow()
	{
		if (!m_Layer.m_ShowPeriodicTableWindow)
			return;

		ImGui::SetNextWindowSize(ImVec2(760.0f, 430.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Periodic Table", &m_Layer.m_ShowPeriodicTableWindow))
		{
			ImGui::End();
			return;
		}

		const ImVec2 cellSize(36.0f, 32.0f);
		for (const PeriodicTableRow &row : kPeriodicTableRows)
		{
			for (std::size_t column = 0; column < row.size(); ++column)
			{
				const char *symbol = row[column];
				if (column > 0)
					ImGui::SameLine();

				if (symbol == nullptr || symbol[0] == '\0')
				{
					ImGui::Dummy(cellSize);
					continue;
				}

				if (symbol == m_Layer.m_SelectedPeriodicElement)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.92f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.64f, 0.98f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.84f, 1.0f));
				}
				const bool clicked = ImGui::Button(symbol, cellSize);
				if (symbol == m_Layer.m_SelectedPeriodicElement)
					ImGui::PopStyleColor(3);

				if (clicked)
					m_Layer.m_SelectedPeriodicElement = symbol;
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Lanthanides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < kLanthanides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(kLanthanides[index], cellSize))
				m_Layer.m_SelectedPeriodicElement = kLanthanides[index];
		}
		ImGui::TextUnformatted("Actinides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < kActinides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(kActinides[index], cellSize))
				m_Layer.m_SelectedPeriodicElement = kActinides[index];
		}

		ImGui::Separator();
		ImGui::Text("Selected element: %s", m_Layer.m_SelectedPeriodicElement.c_str());

		ImGui::End();
	}

	void RendererPanel::startCameraTransition(
		RendererWindowState &windowState,
		const glm::vec3 &target,
		float distance,
		float yaw,
		float pitch,
		float roll,
		const char *sourceAction)
	{
		if (windowState.camera == nullptr)
			return;

		const char *resolvedSourceAction =
			(sourceAction != nullptr && sourceAction[0] != '\0')
				? sourceAction
				: "unspecified";
		const float targetDistance = std::max(distance, 0.1f);
		if (windowState.transitionActive)
		{
			const float previousDuration = std::max(0.01f, windowState.transitionDuration);
			const float previousProgress =
				std::clamp(windowState.transitionElapsed / previousDuration, 0.0f, 1.0f);
			DS_LOG_INFO(
				"Renderer transition interrupted prev_source={} progress={:.3f} new_source={}",
				windowState.transitionSourceAction.empty() ? "unspecified" : windowState.transitionSourceAction.c_str(),
				previousProgress,
				resolvedSourceAction);
		}

		const float startYaw = windowState.camera->Yaw();
		const float startPitch = windowState.camera->Pitch();
		const float startRoll = windowState.camera->Roll();
		const float deltaYaw = NormalizeAngleRadians(yaw - startYaw);
		const float deltaPitch = NormalizeAngleRadians(pitch - startPitch);
		const float deltaRoll = NormalizeAngleRadians(roll - startRoll);
		DS_LOG_INFO(
			"Renderer transition start source={} duration={:.3f}s "
			"start_ypr_deg=({:.2f},{:.2f},{:.2f}) end_ypr_deg=({:.2f},{:.2f},{:.2f}) "
			"delta_ypr_deg=({:.2f},{:.2f},{:.2f}) distance=({:.3f}->{:.3f})",
			resolvedSourceAction,
			std::max(0.01f, windowState.transitionDuration),
			RadiansToDegrees(startYaw),
			RadiansToDegrees(startPitch),
			RadiansToDegrees(startRoll),
			RadiansToDegrees(yaw),
			RadiansToDegrees(pitch),
			RadiansToDegrees(roll),
			RadiansToDegrees(deltaYaw),
			RadiansToDegrees(deltaPitch),
			RadiansToDegrees(deltaRoll),
			windowState.camera->Distance(),
			targetDistance);

		windowState.transitionActive = true;
		windowState.transitionElapsed = 0.0f;
		windowState.transitionStartTarget = windowState.camera->Target();
		windowState.transitionEndTarget = target;
		windowState.transitionStartDistance = windowState.camera->Distance();
		windowState.transitionEndDistance = targetDistance;
		windowState.transitionStartYaw = startYaw;
		windowState.transitionEndYaw = yaw;
		windowState.transitionStartPitch = startPitch;
		windowState.transitionEndPitch = pitch;
		windowState.transitionStartRoll = startRoll;
		windowState.transitionEndRoll = roll;
		windowState.transitionSourceAction = resolvedSourceAction;
	}

	void RendererPanel::updateCameraTransition(RendererWindowState &windowState, float deltaTime)
	{
		if (!windowState.transitionActive || windowState.camera == nullptr)
			return;

		windowState.transitionElapsed += std::max(0.0f, deltaTime);
		const float duration = std::max(0.01f, windowState.transitionDuration);
		const float alpha = std::clamp(windowState.transitionElapsed / duration, 0.0f, 1.0f);
		const float t = EaseOutCubic(alpha);

		const glm::vec3 target = glm::mix(windowState.transitionStartTarget, windowState.transitionEndTarget, t);
		const float distance = glm::mix(windowState.transitionStartDistance, windowState.transitionEndDistance, t);
		const float yaw = LerpAngleRadians(windowState.transitionStartYaw, windowState.transitionEndYaw, t);
		const float pitch = LerpAngleRadians(windowState.transitionStartPitch, windowState.transitionEndPitch, t);
		const float roll = LerpAngleRadians(windowState.transitionStartRoll, windowState.transitionEndRoll, t);

		windowState.camera->SetOrbitState(target, distance, yaw, pitch);
		windowState.camera->SetRoll(roll);

		if (alpha >= 1.0f)
		{
			DS_LOG_INFO(
				"Renderer transition complete source={} final_ypr_deg=({:.2f},{:.2f},{:.2f})",
				windowState.transitionSourceAction.empty() ? "unspecified" : windowState.transitionSourceAction.c_str(),
				RadiansToDegrees(yaw),
				RadiansToDegrees(pitch),
				RadiansToDegrees(roll));
			windowState.transitionActive = false;
		}
	}
} // namespace DefectStudio
