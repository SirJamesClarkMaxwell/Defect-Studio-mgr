#pragma once

#include <cstddef>
#include <string>
#include <utility>

#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::EditorUiEvents
{
	enum class PersistKind
	{
		Theme,
		Layout,
	};

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

	struct PersistRequested final : public BusEvent
	{
		PersistRequested(PersistKind persistKind, Path targetPath, std::string fileContents)
			: kind(persistKind),
			  path(std::move(targetPath)),
			  contents(std::move(fileContents))
		{
		}

		PersistKind kind = PersistKind::Theme;
		Path path;
		std::string contents;
	};

	struct ThemeLoaded final : public BusEvent
	{
		ThemeLoaded(Path targetPath, std::string fileContents)
			: path(std::move(targetPath)),
			  contents(std::move(fileContents))
		{
		}

		Path path;
		std::string contents;
	};

	struct ThemeLoadFailed final : public BusEvent
	{
		ThemeLoadFailed(Path targetPath, std::string failureReason)
			: path(std::move(targetPath)),
			  error(std::move(failureReason))
		{
		}

		Path path;
		std::string error;
	};

	struct ThemeSaved final : public BusEvent
	{
		explicit ThemeSaved(Path targetPath)
			: path(std::move(targetPath))
		{
		}

		Path path;
	};

	struct ThemeSaveFailed final : public BusEvent
	{
		ThemeSaveFailed(Path targetPath, std::string failureReason)
			: path(std::move(targetPath)),
			  error(std::move(failureReason))
		{
		}

		Path path;
		std::string error;
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

	struct LayoutSaved final : public BusEvent
	{
		LayoutSaved(Path targetPath, std::size_t writtenByteCount)
			: path(std::move(targetPath)),
			  bytes(writtenByteCount)
		{
		}

		Path path;
		std::size_t bytes = 0;
	};

	struct LayoutLoaded final : public BusEvent
	{
		LayoutLoaded(Path targetPath, std::string fileContents)
			: path(std::move(targetPath)),
			  contents(std::move(fileContents))
		{
		}

		Path path;
		std::string contents;
	};

	struct LayoutLoadFailed final : public BusEvent
	{
		LayoutLoadFailed(Path targetPath, std::string failureReason)
			: path(std::move(targetPath)),
			  error(std::move(failureReason))
		{
		}

		Path path;
		std::string error;
	};

	struct LayoutSaveFailed final : public BusEvent
	{
		LayoutSaveFailed(Path targetPath, std::string failureReason)
			: path(std::move(targetPath)),
			  error(std::move(failureReason))
		{
		}

		Path path;
		std::string error;
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
