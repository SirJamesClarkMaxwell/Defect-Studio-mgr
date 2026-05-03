#pragma once

#include <array>
#include <string>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct UIConfig
	{
		float fontScale = 1.0f;
		float fontScaleStep = 0.1f;
		std::string fontPath;
		bool settingsPreviewEnabled = true;
		bool settingsAutoSaveOnPreview = false;
		float fontScaleMin = 0.7f;
		float fontScaleMax = 10.0f;
		float fontScaleStepMin = 0.01f;
		float fontScaleStepMax = 1.0f;
		float fontScaleStepSliderMax = 0.5f;
	};

	struct AppearanceStateRules
	{
		float neutralHoverLighten = 0.08f;
		float neutralActiveLighten = 0.04f;
		float accentHoverLighten = 0.12f;
		float accentHoverSaturation = 0.04f;
		float accentActiveDarken = 0.08f;
		float accentActiveSaturation = 0.06f;
		float selectedAlpha = 0.28f;
		float selectedHoverAlpha = 0.36f;
		float selectedActiveAlpha = 0.44f;
		float disabledAlpha = 0.45f;
		float disabledBackgroundMix = 0.55f;
	};

	struct AppearanceConfig
	{
		std::array<float, 4> backgroundColor = {0.067f, 0.063f, 0.055f, 1.0f};
		std::array<float, 4> surfaceColor = {0.098f, 0.090f, 0.082f, 1.0f};
		std::array<float, 4> raisedSurfaceColor = {0.141f, 0.125f, 0.106f, 1.0f};
		std::array<float, 4> borderColor = {0.227f, 0.188f, 0.157f, 1.0f};
		std::array<float, 4> collapsibleColor = {0.941f, 0.541f, 0.141f, 1.0f};
		std::array<float, 4> textColor = {0.949f, 0.906f, 0.847f, 1.0f};
		std::array<float, 4> mutedTextColor = {0.663f, 0.604f, 0.545f, 1.0f};
		std::array<float, 4> accentColor = {0.941f, 0.541f, 0.141f, 1.0f};
		std::array<float, 4> clearColor = {0.10f, 0.10f, 0.12f, 1.00f};
		float windowRounding = 8.0f;
		float frameRounding = 6.0f;
		float grabRounding = 6.0f;
		float popupRounding = 8.0f;
		float scrollbarRounding = 8.0f;
		float tabRounding = 6.0f;
		float windowPaddingX = 12.0f;
		float windowPaddingY = 10.0f;
		float framePaddingX = 9.0f;
		float framePaddingY = 5.0f;
		float itemSpacingX = 8.0f;
		float itemSpacingY = 7.0f;
		AppearanceStateRules stateRules;
	};

	struct ApplicationPaths
	{
		Path installRoot;
		Path appConfigDirectory;
		Path userConfigDirectory;
		Path profilesDirectory;
		Path themesDirectory;
		Path layoutsDirectory;
		Path exportsDirectory;
		Path assetsDirectory;
		Path fontsDirectory;
	};
} // namespace DefectStudio
