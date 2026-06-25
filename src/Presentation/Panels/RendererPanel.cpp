#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "Core/Utils/Logger.hpp"
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
			DS_LOG_DEBUG(
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
		if (!m_Layer.IsAttached())
			return;

		for (RendererWindowState &windowState : m_Layer.GetWindows())
			renderStructureWindow(windowState, deltaTime);

		// drawPeriodicTableWindow();
		m_Layer.CollectProfilingData();
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

		const unsigned int textureId = m_Layer.RenderToFbo(
			windowState.title,
			windowState.structure,
			windowState,
			m_Layer.GetGlobalSettings());

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
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				commitViewInteraction(windowState);
			}
		}

		applyViewportKeyboardNavigation(windowState);

		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
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
			beginViewInteraction(windowState, sourceAction);
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
			beginViewInteraction(windowState, "toolbar.zoom_out");
			windowState.transitionActive = false;
			windowState.camera->Zoom(-std::max(0.5f, windowState.percentStep * 0.1f));
			commitViewInteraction(windowState);
		}
		sameLineTight();

		if (iconButton("##ZoomIn", "plus.png", "+", "Zoom in"))
		{
			beginViewInteraction(windowState, "toolbar.zoom_in");
			windowState.transitionActive = false;
			windowState.camera->Zoom(+std::max(0.5f, windowState.percentStep * 0.1f));
			commitViewInteraction(windowState);
		}
		sameLineTight();

		const bool isOrtho = windowState.camera->Projection() == CameraProjection::Orthographic;
		if (ImGui::Button(isOrtho ? "ORTHO" : "PERSP"))
		{
			beginViewInteraction(windowState, "toolbar.toggle_projection");
			windowState.camera->ToggleProjection();
			commitViewInteraction(windowState);
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
			beginViewInteraction(windowState, sourceAction);
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
			float desiredDistance = m_Layer.GetGlobalSettings().focusSelectedAtomDistance;
			if (m_Layer.GetGlobalSettings().focusSelectedAtomRespectAtomRadius)
			{
				const float radiusDistance = atom.radius * m_Layer.GetGlobalSettings().focusSelectedAtomRadiusMultiplier;
				desiredDistance = std::max(desiredDistance, radiusDistance);
			}
			windowState.transitionDuration = std::max(
				0.02f,
				m_Layer.GetGlobalSettings().focusSelectedAtomTransitionSeconds);

			beginViewInteraction(windowState, "keyboard.focus_selected_atom");
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
		const RendererKeyboardShortcutSettings &shortcuts = m_Layer.GetGlobalSettings().shortcuts;

		const auto shortcutPressed = [](ImGuiKey key) -> bool
		{
			return key != ImGuiKey_None && ImGui::IsKeyPressed(key);
		};
		const auto shortcutDown = [](ImGuiKey key) -> bool
		{
			return key != ImGuiKey_None && ImGui::IsKeyDown(key);
		};

		if (io.KeyCtrl && shortcutPressed(ImGuiKey_Z))
		{
			undoViewChange(windowState);
			return;
		}
		if (io.KeyCtrl && shortcutPressed(ImGuiKey_Y))
		{
			redoViewChange(windowState);
			return;
		}

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
				m_Layer.GetGlobalSettings().rotationSpeed,
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
				m_Layer.GetGlobalSettings().rotationSpeed,
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
				m_Layer.GetGlobalSettings().rotationSpeed,
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
				m_Layer.GetGlobalSettings().rotationSpeed,
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
				m_Layer.GetGlobalSettings().rotationSpeed,
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
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(-rotationDeltaRadians);
			queueTransition(animated, "keyboard.roll_right");
		}

		if (shortcutPressed(shortcuts.zoomIn))
		{
			beginViewInteraction(windowState, "keyboard.zoom_in");
			windowState.transitionActive = false;
			windowState.camera->Zoom(+std::max(0.5f, windowState.percentStep * 0.1f));
		}
		if (shortcutPressed(shortcuts.zoomOut))
		{
			beginViewInteraction(windowState, "keyboard.zoom_out");
			windowState.transitionActive = false;
			windowState.camera->Zoom(-std::max(0.5f, windowState.percentStep * 0.1f));
		}

		if (shortcutPressed(shortcuts.focusSelectedAtom))
			focusSelectedAtom();

		const bool continuousShortcutDown =
			shortcutDown(shortcuts.orbitLeft) ||
			shortcutDown(shortcuts.orbitRight) ||
			shortcutDown(shortcuts.orbitUp) ||
			shortcutDown(shortcuts.orbitDown) ||
			shortcutDown(shortcuts.rollLeft) ||
			shortcutDown(shortcuts.rollRight) ||
			shortcutDown(shortcuts.zoomIn) ||
			shortcutDown(shortcuts.zoomOut);
		if (!continuousShortcutDown &&
			windowState.viewInteractionActive &&
			windowState.viewInteractionSource.rfind("keyboard.", 0) == 0 &&
			!windowState.transitionActive)
		{
			commitViewInteraction(windowState);
		}
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

		const bool touchpadOrbit = m_Layer.GetGlobalSettings().touchpadNavigation && altPressed && lmb;
		const bool touchpadPan = m_Layer.GetGlobalSettings().touchpadNavigation && altPressed && shiftPressed && lmb;
		const bool touchpadZoom = m_Layer.GetGlobalSettings().touchpadNavigation && altPressed && rmb;
		const bool dragActiveInput = mmb || touchpadOrbit || touchpadPan || touchpadZoom;
		if (dragActiveInput)
			windowState.transitionActive = false;

		if (!dragActiveInput)
		{
			windowState.dragActive = false;
			windowState.lastMousePosition = io.MousePos;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				commitViewInteraction(windowState);
			}
		}

		float wheel = io.MouseWheel;
		if (m_Layer.GetGlobalSettings().invertZoom)
			wheel = -wheel;
		if (wheel != 0.0f)
		{
			beginViewInteraction(windowState, "mouse.wheel_zoom");
			windowState.transitionActive = false;
			windowState.camera->Zoom(wheel * m_Layer.GetGlobalSettings().zoomSensitivity);
			commitViewInteraction(windowState);
		}

		if (!dragActiveInput)
			return;

		if (!windowState.dragActive)
		{
			const char *sourceAction = touchpadZoom
				? "mouse.touchpad_zoom"
				: ((mmb && shiftPressed) || touchpadPan)
					? "mouse.pan"
					: "mouse.orbit";
			beginViewInteraction(windowState, sourceAction);
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
				(-delta.y * 0.020f) * m_Layer.GetGlobalSettings().zoomSensitivity);
			return;
		}

		if ((mmb && shiftPressed) || touchpadPan)
		{
			windowState.camera->Pan(
				delta.x * m_Layer.GetGlobalSettings().panSensitivity,
				delta.y * m_Layer.GetGlobalSettings().panSensitivity);
			return;
		}

		windowState.camera->Orbit(
			delta.x * m_Layer.GetGlobalSettings().orbitSensitivity,
			delta.y * m_Layer.GetGlobalSettings().orbitSensitivity);
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
		if (!m_Layer.GetShowPeriodicTableWindow())
			return;

		ImGui::SetNextWindowSize(ImVec2(760.0f, 430.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Periodic Table", &m_Layer.GetShowPeriodicTableWindow()))
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

				if (symbol == m_Layer.GetSelectedPeriodicElement())
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.92f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.64f, 0.98f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.84f, 1.0f));
				}
				const bool clicked = ImGui::Button(symbol, cellSize);
				if (symbol == m_Layer.GetSelectedPeriodicElement())
					ImGui::PopStyleColor(3);

				if (clicked)
					m_Layer.GetSelectedPeriodicElement() = symbol;
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
				m_Layer.GetSelectedPeriodicElement() = kLanthanides[index];
		}
		ImGui::TextUnformatted("Actinides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < kActinides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(kActinides[index], cellSize))
				m_Layer.GetSelectedPeriodicElement() = kActinides[index];
		}

		ImGui::Separator();
		ImGui::Text("Selected element: %s", m_Layer.GetSelectedPeriodicElement().c_str());

		ImGui::End();
	}

	RendererViewSnapshot RendererPanel::captureViewSnapshot(const RendererWindowState &windowState) const
	{
		RendererViewSnapshot snapshot;
		if (windowState.camera == nullptr)
			return snapshot;

		snapshot.target = windowState.camera->Target();
		snapshot.distance = windowState.camera->Distance();
		snapshot.yaw = windowState.camera->Yaw();
		snapshot.pitch = windowState.camera->Pitch();
		snapshot.roll = windowState.camera->Roll();
		snapshot.projection = windowState.camera->Projection();
		return snapshot;
	}

	void RendererPanel::restoreViewSnapshot(
		RendererWindowState &windowState,
		const RendererViewSnapshot &snapshot,
		const char *sourceAction)
	{
		if (windowState.camera == nullptr)
			return;

		windowState.camera->SetProjection(snapshot.projection);
		windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
			m_Layer.GetGlobalSettings().rotationSpeed);
		startCameraTransition(
			windowState,
			snapshot.target,
			snapshot.distance,
			snapshot.yaw,
			snapshot.pitch,
			snapshot.roll,
			sourceAction);
	}

	void RendererPanel::beginViewInteraction(RendererWindowState &windowState, const char *sourceAction)
	{
		if (windowState.camera == nullptr || windowState.viewInteractionActive)
			return;

		windowState.viewInteractionActive = true;
		windowState.viewInteractionSource =
			(sourceAction != nullptr && sourceAction[0] != '\0') ? sourceAction : "view.change";
		windowState.viewInteractionStart = captureViewSnapshot(windowState);
	}

	void RendererPanel::commitViewInteraction(RendererWindowState &windowState)
	{
		if (!windowState.viewInteractionActive)
			return;

		const RendererViewSnapshot before = windowState.viewInteractionStart;
		const RendererViewSnapshot after = captureViewSnapshot(windowState);
		const std::string source = windowState.viewInteractionSource;
		windowState.viewInteractionActive = false;
		windowState.viewInteractionSource.clear();
		pushViewChange(windowState, before, after, source.c_str());
	}

	void RendererPanel::cancelViewInteraction(RendererWindowState &windowState)
	{
		windowState.viewInteractionActive = false;
		windowState.viewInteractionSource.clear();
	}

	void RendererPanel::pushViewChange(
		RendererWindowState &windowState,
		const RendererViewSnapshot &before,
		const RendererViewSnapshot &after,
		const char *sourceAction)
	{
		constexpr float kEpsilon = 0.0001f;
		const bool sameTarget = glm::length(before.target - after.target) <= kEpsilon;
		const bool sameScalars =
			std::abs(before.distance - after.distance) <= kEpsilon &&
			std::abs(RendererViewCamera::NormalizeAngleRadians(before.yaw - after.yaw)) <= kEpsilon &&
			std::abs(RendererViewCamera::NormalizeAngleRadians(before.pitch - after.pitch)) <= kEpsilon &&
			std::abs(RendererViewCamera::NormalizeAngleRadians(before.roll - after.roll)) <= kEpsilon &&
			before.projection == after.projection;
		if (sameTarget && sameScalars)
			return;

		RendererViewStateChange change;
		change.description = sourceAction != nullptr ? sourceAction : "view.change";
		change.before = before;
		change.after = after;
		windowState.viewUndoHistory.push_back(std::move(change));
		constexpr std::size_t kMaxViewHistoryEntries = 256u;
		if (windowState.viewUndoHistory.size() > kMaxViewHistoryEntries)
			windowState.viewUndoHistory.erase(windowState.viewUndoHistory.begin());
		windowState.viewRedoHistory.clear();
	}

	void RendererPanel::undoViewChange(RendererWindowState &windowState)
	{
		if (windowState.viewInteractionActive)
			commitViewInteraction(windowState);
		if (windowState.viewUndoHistory.empty())
			return;

		RendererViewStateChange change = std::move(windowState.viewUndoHistory.back());
		windowState.viewUndoHistory.pop_back();
		restoreViewSnapshot(windowState, change.before, "view.undo");
		windowState.viewRedoHistory.push_back(std::move(change));
	}

	void RendererPanel::redoViewChange(RendererWindowState &windowState)
	{
		if (windowState.viewInteractionActive)
			cancelViewInteraction(windowState);
		if (windowState.viewRedoHistory.empty())
			return;

		RendererViewStateChange change = std::move(windowState.viewRedoHistory.back());
		windowState.viewRedoHistory.pop_back();
		restoreViewSnapshot(windowState, change.after, "view.redo");
		windowState.viewUndoHistory.push_back(std::move(change));
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
			DS_LOG_DEBUG(
				"Renderer transition interrupted prev_source={} progress={:.3f} new_source={}",
				windowState.transitionSourceAction.empty() ? "unspecified" : windowState.transitionSourceAction.c_str(),
				previousProgress,
				resolvedSourceAction);
		}

		const float startYaw = windowState.camera->Yaw();
		const float startPitch = windowState.camera->Pitch();
		const float startRoll = windowState.camera->Roll();
		const glm::quat startOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(startYaw, startPitch, startRoll);
		glm::quat endOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(yaw, pitch, roll);
		if (glm::dot(startOrientation, endOrientation) < 0.0f)
			endOrientation = -endOrientation;
		const float deltaYaw = RendererViewCamera::NormalizeAngleRadians(yaw - startYaw);
		const float deltaPitch = RendererViewCamera::NormalizeAngleRadians(pitch - startPitch);
		const float deltaRoll = RendererViewCamera::NormalizeAngleRadians(roll - startRoll);
		const float angularDeltaDegrees = RadiansToDegrees(
			2.0f * std::acos(glm::clamp(std::abs(glm::dot(startOrientation, endOrientation)), 0.0f, 1.0f)));
		DS_LOG_DEBUG(
			"Renderer transition start source={} duration={:.3f}s "
			"start_ypr_deg=({:.2f},{:.2f},{:.2f}) end_ypr_deg=({:.2f},{:.2f},{:.2f}) "
			"delta_ypr_deg=({:.2f},{:.2f},{:.2f}) angular_delta_deg={:.2f} distance=({:.3f}->{:.3f})",
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
			angularDeltaDegrees,
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
		windowState.transitionStartOrientation = startOrientation;
		windowState.transitionEndOrientation = endOrientation;
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
		const glm::quat orientation =
			glm::normalize(glm::slerp(windowState.transitionStartOrientation, windowState.transitionEndOrientation, t));
		float yaw = 0.0f;
		float pitch = 0.0f;
		float roll = 0.0f;
		RendererViewCamera::CameraEulerFromOrientationQuat(orientation, yaw, pitch, roll);

		windowState.camera->SetOrbitState(target, distance, yaw, pitch);
		windowState.camera->SetRoll(roll);

		if (alpha >= 1.0f)
		{
			DS_LOG_DEBUG(
				"Renderer transition complete source={} final_ypr_deg=({:.2f},{:.2f},{:.2f})",
				windowState.transitionSourceAction.empty() ? "unspecified" : windowState.transitionSourceAction.c_str(),
				RadiansToDegrees(yaw),
				RadiansToDegrees(pitch),
				RadiansToDegrees(roll));
			windowState.transitionActive = false;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) != 0 &&
				windowState.viewInteractionSource.rfind("keyboard.", 0) != 0)
			{
				commitViewInteraction(windowState);
			}
		}
	}
} // namespace DefectStudio
