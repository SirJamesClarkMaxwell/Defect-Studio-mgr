#pragma once

#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	enum class CameraProjection
	{
		Perspective,
		Orthographic
	};

	struct RendererLightingSettings
	{
		glm::vec3 keyDirection = glm::normalize(glm::vec3(0.6f, 0.8f, 0.5f));
		glm::vec3 fillDirection = glm::normalize(glm::vec3(-0.7f, 0.3f, 0.2f));
		glm::vec3 backDirection = glm::normalize(glm::vec3(0.0f, -0.4f, -0.8f));
		float ambientIntensity = 0.18f;
		float keyIntensity = 0.55f;
		float fillIntensity = 0.25f;
		float backIntensity = 0.12f;
		bool twoSided = true;
	};

	struct RendererViewportSettings
	{
		float axisButtonSize = 20.0f;
		float iconButtonSize = 18.0f;
	};

	struct RendererToolbarWheelSettings
	{
		float rotationStepDelta = 1.0f;
		float zoomStepDelta = 1.0f;
		std::vector<float> ctrlPresetValues = {
			0.0f,
			1.0f,
			3.0f,
			5.0f,
			10.0f,
			15.0f,
			30.0f,
			45.0f,
			60.0f,
			90.0f,
			180.0f};
	};

	struct RendererGridSettings
	{
		bool autoFitToStructureBounds = true;
		float paddingPercent = 20.0f;
		float spacing = 0.7f;
		float planeZ = 0.0f;
	};

	struct RendererKeyboardShortcutSettings
	{
		ImGuiKey alignAxisA = ImGuiKey_A;
		ImGuiKey alignAxisB = ImGuiKey_B;
		ImGuiKey alignAxisC = ImGuiKey_C;
		ImGuiKey orbitLeft = ImGuiKey_LeftArrow;
		ImGuiKey orbitRight = ImGuiKey_RightArrow;
		ImGuiKey orbitUp = ImGuiKey_UpArrow;
		ImGuiKey orbitDown = ImGuiKey_DownArrow;
		ImGuiKey rollLeft = ImGuiKey_Q;
		ImGuiKey rollRight = ImGuiKey_E;
		ImGuiKey zoomIn = ImGuiKey_R;
		ImGuiKey zoomOut = ImGuiKey_F;
		ImGuiKey focusSelectedAtom = ImGuiKey_Period;
	};

	struct RendererGlobalRenderSettings
	{
		glm::vec4 backgroundColor = glm::vec4(0.06f, 0.07f, 0.08f, 1.0f);
		float orbitSensitivity = 1.0f;
		float panSensitivity = 1.0f;
		float zoomSensitivity = 1.0f;
		float rotationSpeed = 1.0f;
		float focusSelectedAtomDistance = 3.0f;
		float focusSelectedAtomTransitionSeconds = 0.18f;
		bool focusSelectedAtomRespectAtomRadius = true;
		float focusSelectedAtomRadiusMultiplier = 2.0f;
		bool invertZoom = false;
		bool touchpadNavigation = true;
		CameraProjection defaultCameraProjection = CameraProjection::Perspective;
		RendererLightingSettings lighting;
		RendererViewportSettings viewport;
		RendererToolbarWheelSettings toolbarWheel;
		RendererGridSettings grid;
		RendererKeyboardShortcutSettings shortcuts;
	};
} // namespace DefectStudio
