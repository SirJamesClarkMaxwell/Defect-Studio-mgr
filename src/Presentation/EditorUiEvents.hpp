#pragma once

#include <utility>

#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct UiConfigPreviewRequestedEvent final : public BusEvent
	{
		explicit UiConfigPreviewRequestedEvent(UIConfig uiConfig)
			: ui(std::move(uiConfig))
		{
		}

		UIConfig ui;
	};

	struct UiFontListRefreshRequestedEvent final : public BusEvent
	{
	};

	struct UiFontReloadRequestedEvent final : public BusEvent
	{
	};

	struct UiFontScaleChangedEvent final : public BusEvent
	{
		explicit UiFontScaleChangedEvent(float requestedFontScale)
			: fontScale(requestedFontScale)
		{
		}

		float fontScale = 1.0f;
	};

	struct UiAppearancePreviewRequestedEvent final : public BusEvent
	{
		explicit UiAppearancePreviewRequestedEvent(AppearanceConfig appearanceConfig)
			: appearance(std::move(appearanceConfig))
		{
		}

		AppearanceConfig appearance;
	};

	struct UiAppearanceApplyRequestedEvent final : public BusEvent
	{
		explicit UiAppearanceApplyRequestedEvent(AppearanceConfig appearanceConfig)
			: appearance(std::move(appearanceConfig))
		{
		}

		AppearanceConfig appearance;
	};

	struct UiThemeSaveRequestedEvent final : public BusEvent
	{
		UiThemeSaveRequestedEvent(Path targetPath, AppearanceConfig appearanceConfig)
			: path(std::move(targetPath)),
			  appearance(std::move(appearanceConfig))
		{
		}

		Path path;
		AppearanceConfig appearance;
	};

	struct UiThemeLoadRequestedEvent final : public BusEvent
	{
		explicit UiThemeLoadRequestedEvent(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct UiLayoutSaveRequestedEvent final : public BusEvent
	{
		explicit UiLayoutSaveRequestedEvent(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct UiLayoutLoadRequestedEvent final : public BusEvent
	{
		explicit UiLayoutLoadRequestedEvent(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct UiLayoutResetRequestedEvent final : public BusEvent
	{
		explicit UiLayoutResetRequestedEvent(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};
} // namespace DefectStudio
