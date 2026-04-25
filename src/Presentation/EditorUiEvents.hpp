#pragma once

#include <utility>

#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::EditorUiEvents
{
	struct ConfigPreviewRequested final : public BusEvent
	{
		explicit ConfigPreviewRequested(UIConfig uiConfig)
			: ui(std::move(uiConfig))
		{
		}

		UIConfig ui;
	};

	struct FontListRefreshRequested final : public BusEvent
	{
	};

	struct FontReloadRequested final : public BusEvent
	{
	};

	struct FontScaleChanged final : public BusEvent
	{
		explicit FontScaleChanged(float requestedFontScale)
			: fontScale(requestedFontScale)
		{
		}

		float fontScale = 1.0f;
	};

	struct AppearancePreviewRequested final : public BusEvent
	{
		explicit AppearancePreviewRequested(AppearanceConfig appearanceConfig)
			: appearance(std::move(appearanceConfig))
		{
		}

		AppearanceConfig appearance;
	};

	struct AppearanceApplyRequested final : public BusEvent
	{
		explicit AppearanceApplyRequested(AppearanceConfig appearanceConfig)
			: appearance(std::move(appearanceConfig))
		{
		}

		AppearanceConfig appearance;
	};

	struct ThemeSaveRequested final : public BusEvent
	{
		ThemeSaveRequested(Path targetPath, AppearanceConfig appearanceConfig)
			: path(std::move(targetPath)),
			  appearance(std::move(appearanceConfig))
		{
		}

		Path path;
		AppearanceConfig appearance;
	};

	struct ThemeLoadRequested final : public BusEvent
	{
		explicit ThemeLoadRequested(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct LayoutSaveRequested final : public BusEvent
	{
		explicit LayoutSaveRequested(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct LayoutLoadRequested final : public BusEvent
	{
		explicit LayoutLoadRequested(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct LayoutResetRequested final : public BusEvent
	{
		explicit LayoutResetRequested(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};
} // namespace DefectStudio::EditorUiEvents
