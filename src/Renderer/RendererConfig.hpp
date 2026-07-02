#pragma once

#include <array>
#include <string>
#include <vector>

namespace DefectStudio
{
	struct RendererLightingConfig
	{
		std::array<float, 3> keyDirection = {0.6f, 0.8f, 0.5f};
		std::array<float, 3> fillDirection = {-0.7f, 0.3f, 0.2f};
		std::array<float, 3> backDirection = {0.0f, -0.4f, -0.8f};
		float ambientIntensity = 0.18f;
		float keyIntensity = 0.55f;
		float fillIntensity = 0.25f;
		float backIntensity = 0.12f;
		bool twoSided = true;
	};

	struct RendererViewportConfig
	{
		float axisButtonSize = 20.0f;
		float iconButtonSize = 18.0f;
	};

	struct RendererToolbarWheelConfig
	{
		float rotationStepDelta = 1.0f;
		float zoomStepDelta = 1.0f;
		std::vector<float> ctrlPresetValues = {0.0f, 1.0f, 3.0f, 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f, 180.0f};
	};

	struct RendererGridConfig
	{
		bool autoFitToStructureBounds = true;
		float paddingPercent = 20.0f;
		float spacing = 0.7f;
		float planeZ = 0.0f;
	};

	struct RendererConfig
	{
		std::array<float, 4> backgroundColor = {0.06f, 0.07f, 0.08f, 1.0f};
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
		std::string defaultProjection = "perspective";
		RendererLightingConfig lighting;
		RendererViewportConfig viewport;
		RendererToolbarWheelConfig toolbarWheel;
		RendererGridConfig grid;
	};
} // namespace DefectStudio
