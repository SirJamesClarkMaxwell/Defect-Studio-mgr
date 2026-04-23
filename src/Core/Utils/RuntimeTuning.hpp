#pragma once

#include <cstddef>

namespace DefectStudio
{
	namespace RuntimeDefaults
	{
		inline constexpr const char *LogFilePath = "logs/DefectStudio.log";
		inline constexpr int WindowWidth = 1280;
		inline constexpr int WindowHeight = 720;
		inline constexpr const char *WindowTitle = "DefectStudio";

		inline constexpr float UiFontScale = 1.0f;
		inline constexpr float UiFontScaleStep = 0.10f;
		inline constexpr const char *UiFontPath = "";
		inline constexpr const char *UiClearColor = "0.10, 0.10, 0.12, 1.00";

		inline constexpr float UiFontScaleMin = 0.70f;
		inline constexpr float UiFontScaleMax = 10.00f;
		inline constexpr float UiFontScaleStepMin = 0.01f;
		inline constexpr float UiFontScaleStepMax = 1.00f;
		inline constexpr float UiFontScaleStepSliderMax = 0.50f;

		inline constexpr int JobsDefaultWorkerThreads = 5;
		inline constexpr bool JobsReserveUrgentWorker = true;

		inline constexpr std::size_t EventQueueInitialCapacity = 256;
		inline constexpr std::size_t EventQueueGrowthStep = 256;
		inline constexpr std::size_t EventQueueMinCapacity = 32;
		inline constexpr std::size_t EventQueueMinGrowthStep = 32;
	}
} // namespace DefectStudio
