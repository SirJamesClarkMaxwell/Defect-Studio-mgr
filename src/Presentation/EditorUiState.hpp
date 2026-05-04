#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Presentation/UiConfig.hpp"

namespace DefectStudio
{
	struct EditorFontOption
	{
		std::string label;
		std::string source;
		std::string path;
	};

	struct AppearancePreview
	{
		AppearanceConfig previewAppearance;
		bool isPreviewActive = false;
		std::string previewStatusMessage;
	};

	struct EditorUiState
	{
		float fontScale = 1.0f;
		float fontScaleStep = 0.1f;
		float fontScaleMin = 0.7f;
		float fontScaleMax = 10.0f;
		float fontScaleStepMin = 0.01f;
		float fontScaleStepMax = 1.0f;
		float fontScaleStepSliderMax = 0.5f;
		int defaultWorkerThreadCount = 1;
		bool reserveUrgentWorkerByDefault = true;
		ApplicationPaths paths;
		AppearanceConfig appearance;
		AppearancePreview appearancePreview;

		std::vector<EditorFontOption> fontOptions;
		std::size_t selectedFontIndex = 0;
		std::string selectedFontPath;
		std::string fontStatusMessage;
		std::string themeSavePath;
		std::string themeLoadPath;
		std::string layoutPath;
		std::vector<Path> availableLayouts;
		std::string appearanceStatusMessage;
		std::string layoutStatusMessage;
	};
} // namespace DefectStudio
