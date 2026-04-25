#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <sstream>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "App/ConfigManager.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/RuntimeTuning.hpp"

namespace DefectStudio
{
	namespace ConfigSchema
	{
		enum class RootKey
		{
			SchemaVersion,
			TraceEvents,
		};

		enum class SectionKey
		{
			Appearance,
			EventQueue,
			Jobs,
			Log,
			UI,
			Window,
		};

		enum class AppearanceKey
		{
			Colors,
			ClearColor,
			Rounding,
			Spacing,
			StateRules,
		};

		enum class AppearanceColorKey
		{
			Accent,
			Background,
			Border,
			Collapsible,
			MutedText,
			RaisedSurface,
			Surface,
			Text,
		};

		enum class AppearanceRoundingKey
		{
			Frame,
			Grab,
			Popup,
			Scrollbar,
			Tab,
			Window,
		};

		enum class AppearanceSpacingKey
		{
			FramePadding,
			ItemSpacing,
			WindowPadding,
			X,
			Y,
		};

		enum class AppearanceStateRuleKey
		{
			AccentActiveDarken,
			AccentActiveSaturation,
			AccentHoverLighten,
			AccentHoverSaturation,
			DisabledAlpha,
			DisabledBackgroundMix,
			NeutralActiveLighten,
			NeutralHoverLighten,
			SelectedActiveAlpha,
			SelectedAlpha,
			SelectedHoverAlpha,
		};

		enum class EventQueueKey
		{
			GrowthStep,
			InitialCapacity,
		};

		enum class JobsKey
		{
			DefaultWorkerThreads,
			ReserveUrgentWorker,
		};

		enum class LogKey
		{
			FilePath,
			Level,
			ToFile,
		};

		enum class UiKey
		{
			Font,
			FontPath,
			SettingsAutoSaveOnPreview,
			SettingsPreviewEnabled,
			FontScale,
			FontScaleMax,
			FontScaleMin,
			FontScaleStep,
			FontScaleStepMax,
			FontScaleStepMin,
			FontScaleStepSliderMax,
		};

		enum class WindowKey
		{
			Height,
			Maximized,
			Title,
			Width,
			X,
			Y,
		};

		enum class LegacyKey
		{
			AppearanceClearColor,
			EventQueueGrowthStep,
			EventQueueInitialCapacity,
			JobsDefaultWorkerThreads,
			JobsReserveUrgentWorker,
			LogFilePath,
			LogLevel,
			LogToFile,
			UiFontPath,
			UiSettingsAutoSaveOnPreview,
			UiSettingsPreviewEnabled,
			UiFontScale,
			UiFontScaleMax,
			UiFontScaleMin,
			UiFontScaleStep,
			UiFontScaleStepMax,
			UiFontScaleStepMin,
			UiFontScaleStepSliderMax,
			WindowHeight,
			WindowMaximized,
			WindowTitle,
			WindowWidth,
			WindowX,
			WindowY,
		};

		inline const std::unordered_map<RootKey, const char *> RootKeyNames = {
			{RootKey::SchemaVersion, "schema_version"},
			{RootKey::TraceEvents, "trace_events"},
		};

		inline const std::unordered_map<SectionKey, const char *> SectionKeyNames = {
			{SectionKey::Appearance, "appearance"},
			{SectionKey::EventQueue, "event_queue"},
			{SectionKey::Jobs, "jobs"},
			{SectionKey::Log, "log"},
			{SectionKey::UI, "ui"},
			{SectionKey::Window, "window"},
		};

		inline const std::unordered_map<AppearanceKey, const char *> AppearanceKeyNames = {
			{AppearanceKey::Colors, "colors"},
			{AppearanceKey::ClearColor, "clear_color"},
			{AppearanceKey::Rounding, "rounding"},
			{AppearanceKey::Spacing, "spacing"},
			{AppearanceKey::StateRules, "state_rules"},
		};

		inline const std::unordered_map<AppearanceColorKey, const char *> AppearanceColorKeyNames = {
			{AppearanceColorKey::Accent, "accent"},
			{AppearanceColorKey::Background, "background"},
			{AppearanceColorKey::Border, "border"},
			{AppearanceColorKey::Collapsible, "collapsible"},
			{AppearanceColorKey::MutedText, "muted_text"},
			{AppearanceColorKey::RaisedSurface, "raised_surface"},
			{AppearanceColorKey::Surface, "surface"},
			{AppearanceColorKey::Text, "text"},
		};

		inline const std::unordered_map<AppearanceRoundingKey, const char *> AppearanceRoundingKeyNames = {
			{AppearanceRoundingKey::Frame, "frame"},
			{AppearanceRoundingKey::Grab, "grab"},
			{AppearanceRoundingKey::Popup, "popup"},
			{AppearanceRoundingKey::Scrollbar, "scrollbar"},
			{AppearanceRoundingKey::Tab, "tab"},
			{AppearanceRoundingKey::Window, "window"},
		};

		inline const std::unordered_map<AppearanceSpacingKey, const char *> AppearanceSpacingKeyNames = {
			{AppearanceSpacingKey::FramePadding, "frame_padding"},
			{AppearanceSpacingKey::ItemSpacing, "item_spacing"},
			{AppearanceSpacingKey::WindowPadding, "window_padding"},
			{AppearanceSpacingKey::X, "x"},
			{AppearanceSpacingKey::Y, "y"},
		};

		inline const std::unordered_map<AppearanceStateRuleKey, const char *> AppearanceStateRuleKeyNames = {
			{AppearanceStateRuleKey::AccentActiveDarken, "accent_active_darken"},
			{AppearanceStateRuleKey::AccentActiveSaturation, "accent_active_saturation"},
			{AppearanceStateRuleKey::AccentHoverLighten, "accent_hover_lighten"},
			{AppearanceStateRuleKey::AccentHoverSaturation, "accent_hover_saturation"},
			{AppearanceStateRuleKey::DisabledAlpha, "disabled_alpha"},
			{AppearanceStateRuleKey::DisabledBackgroundMix, "disabled_background_mix"},
			{AppearanceStateRuleKey::NeutralActiveLighten, "neutral_active_lighten"},
			{AppearanceStateRuleKey::NeutralHoverLighten, "neutral_hover_lighten"},
			{AppearanceStateRuleKey::SelectedActiveAlpha, "selected_active_alpha"},
			{AppearanceStateRuleKey::SelectedAlpha, "selected_alpha"},
			{AppearanceStateRuleKey::SelectedHoverAlpha, "selected_hover_alpha"},
		};

		inline const std::unordered_map<EventQueueKey, const char *> EventQueueKeyNames = {
			{EventQueueKey::GrowthStep, "growth_step"},
			{EventQueueKey::InitialCapacity, "initial_capacity"},
		};

		inline const std::unordered_map<JobsKey, const char *> JobsKeyNames = {
			{JobsKey::DefaultWorkerThreads, "default_worker_threads"},
			{JobsKey::ReserveUrgentWorker, "reserve_urgent_worker"},
		};

		inline const std::unordered_map<LogKey, const char *> LogKeyNames = {
			{LogKey::FilePath, "file_path"},
			{LogKey::Level, "level"},
			{LogKey::ToFile, "to_file"},
		};

		inline const std::unordered_map<UiKey, const char *> UiKeyNames = {
			{UiKey::Font, "font"},
			{UiKey::FontPath, "path"},
			{UiKey::SettingsAutoSaveOnPreview, "settings_auto_save_on_preview"},
			{UiKey::SettingsPreviewEnabled, "settings_preview_enabled"},
			{UiKey::FontScale, "font_scale"},
			{UiKey::FontScaleMax, "max"},
			{UiKey::FontScaleMin, "min"},
			{UiKey::FontScaleStep, "font_scale_step"},
			{UiKey::FontScaleStepMax, "max"},
			{UiKey::FontScaleStepMin, "min"},
			{UiKey::FontScaleStepSliderMax, "slider_max"},
		};

		inline const std::unordered_map<WindowKey, const char *> WindowKeyNames = {
			{WindowKey::Height, "height"},
			{WindowKey::Maximized, "maximized"},
			{WindowKey::Title, "title"},
			{WindowKey::Width, "width"},
			{WindowKey::X, "x"},
			{WindowKey::Y, "y"},
		};

		inline const std::unordered_map<LegacyKey, const char *> LegacyKeyNames = {
			{LegacyKey::AppearanceClearColor, "clear_color"},
			{LegacyKey::EventQueueGrowthStep, "event_queue.growth_step"},
			{LegacyKey::EventQueueInitialCapacity, "event_queue.initial_capacity"},
			{LegacyKey::JobsDefaultWorkerThreads, "jobs.default_worker_threads"},
			{LegacyKey::JobsReserveUrgentWorker, "jobs.reserve_urgent_worker"},
			{LegacyKey::LogFilePath, "log.file_path"},
			{LegacyKey::LogLevel, "log.level"},
			{LegacyKey::LogToFile, "log.to_file"},
			{LegacyKey::UiFontPath, "font_path"},
			{LegacyKey::UiSettingsAutoSaveOnPreview, "ui.settings_auto_save_on_preview"},
			{LegacyKey::UiSettingsPreviewEnabled, "ui.settings_preview_enabled"},
			{LegacyKey::UiFontScale, "font_scale"},
			{LegacyKey::UiFontScaleMax, "ui.font_scale.max"},
			{LegacyKey::UiFontScaleMin, "ui.font_scale.min"},
			{LegacyKey::UiFontScaleStep, "font_scale_step"},
			{LegacyKey::UiFontScaleStepMax, "ui.font_scale_step.max"},
			{LegacyKey::UiFontScaleStepMin, "ui.font_scale_step.min"},
			{LegacyKey::UiFontScaleStepSliderMax, "ui.font_scale_step.slider_max"},
			{LegacyKey::WindowHeight, "window.height"},
			{LegacyKey::WindowMaximized, "window.maximized"},
			{LegacyKey::WindowTitle, "window.title"},
			{LegacyKey::WindowWidth, "window.width"},
			{LegacyKey::WindowX, "window.x"},
			{LegacyKey::WindowY, "window.y"},
		};

		template <typename TEnum>
		const char *NameFromMap(const std::unordered_map<TEnum, const char *> &map, TEnum key)
		{
			return map.at(key);
		}

		inline const char *Name(RootKey key) { return NameFromMap(RootKeyNames, key); }
		inline const char *Name(SectionKey key) { return NameFromMap(SectionKeyNames, key); }
		inline const char *Name(AppearanceKey key) { return NameFromMap(AppearanceKeyNames, key); }
		inline const char *Name(AppearanceColorKey key) { return NameFromMap(AppearanceColorKeyNames, key); }
		inline const char *Name(AppearanceRoundingKey key) { return NameFromMap(AppearanceRoundingKeyNames, key); }
		inline const char *Name(AppearanceSpacingKey key) { return NameFromMap(AppearanceSpacingKeyNames, key); }
		inline const char *Name(AppearanceStateRuleKey key) { return NameFromMap(AppearanceStateRuleKeyNames, key); }
		inline const char *Name(EventQueueKey key) { return NameFromMap(EventQueueKeyNames, key); }
		inline const char *Name(JobsKey key) { return NameFromMap(JobsKeyNames, key); }
		inline const char *Name(LogKey key) { return NameFromMap(LogKeyNames, key); }
		inline const char *Name(UiKey key) { return NameFromMap(UiKeyNames, key); }
		inline const char *Name(WindowKey key) { return NameFromMap(WindowKeyNames, key); }
		inline const char *Name(LegacyKey key) { return NameFromMap(LegacyKeyNames, key); }
	}

	namespace ConfigYaml
	{
		YAML::Node MissingNode()
		{
			return YAML::Node(YAML::NodeType::Undefined);
		}

		YAML::Node FindNodeRecursive(const YAML::Node &current, const std::string_view *path, std::size_t remaining)
		{
			if (remaining == 0)
				return current;

			if (!current || !current.IsMap())
				return MissingNode();

			YAML::Node child = current[std::string(path[0])];
			if (!child.IsDefined())
				return MissingNode();

			return FindNodeRecursive(child, path + 1, remaining - 1);
		}

		YAML::Node FindNode(const YAML::Node &root, std::span<const std::string_view> path)
		{
			if (!root || !root.IsMap())
				return MissingNode();

			return FindNodeRecursive(root, path.data(), path.size());
		}

		YAML::Node FindNode(const YAML::Node &root,
		                    std::span<const std::string_view> path,
		                    std::string_view legacyFlatKey)
		{
			YAML::Node node = FindNode(root, path);
			if (node.IsDefined() || legacyFlatKey.empty() || !root || !root.IsMap())
				return node;

			try
			{
				YAML::Node legacyNode = root[std::string(legacyFlatKey)];
				if (legacyNode.IsDefined())
					return legacyNode;
			}
			catch (const YAML::InvalidNode &)
			{
			}

			return MissingNode();
		}

		YAML::Node FindNode(const YAML::Node &root, std::initializer_list<std::string_view> path)
		{
			return FindNode(root, std::span<const std::string_view>(path.begin(), path.size()));
		}

		YAML::Node FindNode(const YAML::Node &root,
				std::initializer_list<std::string_view> path,
				std::string_view legacyFlatKey)
		{
			return FindNode(root, std::span<const std::string_view>(path.begin(), path.size()), legacyFlatKey);
		}

		template <typename TValue>
		TValue ReadValue(const YAML::Node &root,
				std::span<const std::string_view> path,
				std::string_view legacyFlatKey,
				TValue fallback)
		{
			const YAML::Node node = FindNode(root, path, legacyFlatKey);
			if (!node.IsDefined() || node.IsNull())
				return fallback;

			try
			{
				return node.as<TValue>();
			}
			catch (const YAML::Exception &)
			{
				return fallback;
			}
		}

		template <typename TValue>
		TValue ReadValue(const YAML::Node &root,
		                 std::initializer_list<std::string_view> path,
		                 std::string_view legacyFlatKey,
		                 TValue fallback)
		{
			return ReadValue(root, std::span<const std::string_view>(path.begin(), path.size()), legacyFlatKey, fallback);
		}

		std::string ReadString(const YAML::Node &root,
		                       std::span<const std::string_view> path,
		                       std::string_view legacyFlatKey,
		                       std::string_view fallback)
		{
			return ReadValue<std::string>(root, path, legacyFlatKey, std::string(fallback));
		}

		std::string ReadString(const YAML::Node &root,
		                       std::initializer_list<std::string_view> path,
		                       std::string_view legacyFlatKey,
		                       std::string_view fallback)
		{
			return ReadString(root, std::span<const std::string_view>(path.begin(), path.size()), legacyFlatKey, fallback);
		}

		std::string ToLowerCopy(std::string_view value)
		{
			std::string result(value);
			for (char &character : result)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			return result;
		}

		LogLevel ParseLogLevel(std::string_view value, LogLevel fallback)
		{
			const std::string normalized = ToLowerCopy(value);
			if (normalized == "trace")								return LogLevel::Trace;
			if (normalized == "debug")								return LogLevel::Debug;
			if (normalized == "info")								return LogLevel::Info;
			if (normalized == "warn" || normalized == "warning")	return LogLevel::Warn;
			if (normalized == "error")								return LogLevel::Error;
			if (normalized == "critical")							return  LogLevel::Critical;
			return fallback;
		}

		std::string LogLevelToString(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:     return "trace";
				case LogLevel::Debug:     return "debug";
				case LogLevel::Info:      return "info";
				case LogLevel::Warn:      return "warn";
				case LogLevel::Error:     return "error";
				case LogLevel::Critical:  return "critical";
			}
			return "info";
		}

		std::array<float, 4> ParseColorList(std::string_view text, std::array<float, 4> fallback)
		{
			std::array<float, 4> result = fallback;
			std::string normalized(text);
			std::replace(normalized.begin(), normalized.end(), ',', ' ');
			std::istringstream stream(normalized);
			stream.imbue(std::locale::classic());

			for (float &component : result)
			{
				if (!(stream >> component))
					return fallback;
			}
			return result;
		}

		std::array<float, 4> ReadColorNode(const YAML::Node &node, std::array<float, 4> fallback)
		{
			if (!node.IsDefined() || node.IsNull())
				return fallback;

			try
			{
				if (node.IsSequence())
				{
					std::array<float, 4> result = fallback;
					const std::size_t count = std::min<std::size_t>(node.size(), result.size());
					for (std::size_t index = 0; index < count; ++index)
						result[index] = node[index].as<float>();
					return result;
				}

				if (node.IsScalar())
					return ParseColorList(node.as<std::string>(), fallback);
			}
			catch (const YAML::Exception &)
			{
			}

			return fallback;
		}

		std::array<float, 4> ReadColor(const YAML::Node &root,
		                               std::span<const std::string_view> path,
		                               std::string_view legacyFlatKey,
		                               std::array<float, 4> fallback)
		{
			return ReadColorNode(FindNode(root, path, legacyFlatKey), fallback);
		}

		std::array<float, 4> ReadColor(const YAML::Node &root,
		                               std::initializer_list<std::string_view> path,
		                               std::string_view legacyFlatKey,
		                               std::array<float, 4> fallback)
		{
			return ReadColor(root, std::span<const std::string_view>(path.begin(), path.size()), legacyFlatKey, fallback);
		}

		std::array<float, 4> ReadClearColor(const YAML::Node &root, std::array<float, 4> fallback)
		{
			using namespace ConfigSchema;
			const char *appearanceSection = Name(SectionKey::Appearance);
			const char *uiSection = Name(SectionKey::UI);
			const char *clearColorKey = Name(AppearanceKey::ClearColor);

			YAML::Node node = FindNode(root, {appearanceSection, clearColorKey}, Name(LegacyKey::AppearanceClearColor));
			if (!node.IsDefined() || node.IsNull())
				node = FindNode(root, {uiSection, clearColorKey});
			return ReadColorNode(node, fallback);
		}

		std::string ToPortablePathValue(const ApplicationConfig &config, std::string_view value);
		std::string ResolvePortablePathValue(const ApplicationConfig &config, std::string_view value);

		void ApplyAppearanceYaml(const YAML::Node &root, AppearanceConfig &appearance)
		{
			using namespace ConfigSchema;
			using Path3 = std::array<std::string_view, 3>;
			using Path4 = std::array<std::string_view, 4>;

			const char *appearanceSection = Name(SectionKey::Appearance);
			const char *colorsKey = Name(AppearanceKey::Colors);
			const char *roundingKey = Name(AppearanceKey::Rounding);
			const char *spacingKey = Name(AppearanceKey::Spacing);
			const char *stateRulesKey = Name(AppearanceKey::StateRules);
			const char *windowPaddingKey = Name(AppearanceSpacingKey::WindowPadding);
			const char *framePaddingKey = Name(AppearanceSpacingKey::FramePadding);
			const char *itemSpacingKey = Name(AppearanceSpacingKey::ItemSpacing);
			const char *xKey = Name(AppearanceSpacingKey::X);
			const char *yKey = Name(AppearanceSpacingKey::Y);

			appearance.clearColor = ReadClearColor(root, appearance.clearColor);

			Path3 colorPath = {appearanceSection, colorsKey, Name(AppearanceColorKey::Background)};
			appearance.backgroundColor = ReadColor(root, colorPath, {}, appearance.backgroundColor);

			colorPath[2] = Name(AppearanceColorKey::Surface);
			appearance.surfaceColor = ReadColor(root, colorPath, {}, appearance.surfaceColor);

			colorPath[2] = Name(AppearanceColorKey::RaisedSurface);
			appearance.raisedSurfaceColor = ReadColor(root, colorPath, {}, appearance.raisedSurfaceColor);

			colorPath[2] = Name(AppearanceColorKey::Border);
			appearance.borderColor = ReadColor(root, colorPath, {}, appearance.borderColor);

			colorPath[2] = Name(AppearanceColorKey::Collapsible);
			appearance.collapsibleColor = ReadColor(root, colorPath, {}, appearance.collapsibleColor);

			colorPath[2] = Name(AppearanceColorKey::Text);
			appearance.textColor = ReadColor(root, colorPath, {}, appearance.textColor);

			colorPath[2] = Name(AppearanceColorKey::MutedText);
			appearance.mutedTextColor = ReadColor(root, colorPath, {}, appearance.mutedTextColor);

			colorPath[2] = Name(AppearanceColorKey::Accent);
			appearance.accentColor = ReadColor(root, colorPath, {}, appearance.accentColor);



			Path3 roundingPath = {appearanceSection, roundingKey, Name(AppearanceRoundingKey::Window)};
			appearance.windowRounding = ReadValue(root, roundingPath, {}, appearance.windowRounding);

			roundingPath[2] = Name(AppearanceRoundingKey::Frame);
			appearance.frameRounding = ReadValue(root, roundingPath, {}, appearance.frameRounding);

			roundingPath[2] = Name(AppearanceRoundingKey::Grab);
			appearance.grabRounding = ReadValue(root, roundingPath, {}, appearance.grabRounding);

			roundingPath[2] = Name(AppearanceRoundingKey::Popup);
			appearance.popupRounding = ReadValue(root, roundingPath, {}, appearance.popupRounding);

			roundingPath[2] = Name(AppearanceRoundingKey::Scrollbar);
			appearance.scrollbarRounding = ReadValue(root, roundingPath, {}, appearance.scrollbarRounding);

			roundingPath[2] = Name(AppearanceRoundingKey::Tab);
			appearance.tabRounding = ReadValue(root, roundingPath, {}, appearance.tabRounding);


			Path4 spacingPath = {appearanceSection, spacingKey, windowPaddingKey, xKey};
			appearance.windowPaddingX = ReadValue(root, spacingPath, {}, appearance.windowPaddingX);

			spacingPath[3] = yKey;
			appearance.windowPaddingY = ReadValue(root, spacingPath, {}, appearance.windowPaddingY);

			spacingPath[2] = framePaddingKey;
			spacingPath[3] = xKey;
			appearance.framePaddingX = ReadValue(root, spacingPath, {}, appearance.framePaddingX);

			spacingPath[3] = yKey;
			appearance.framePaddingY = ReadValue(root, spacingPath, {}, appearance.framePaddingY);

			spacingPath[2] = itemSpacingKey;
			spacingPath[3] = xKey;
			appearance.itemSpacingX = ReadValue(root, spacingPath, {}, appearance.itemSpacingX);

			spacingPath[3] = yKey;
			appearance.itemSpacingY = ReadValue(root, spacingPath, {}, appearance.itemSpacingY);


			AppearanceStateRules &rules = appearance.stateRules;
			Path3 stateRulePath = {appearanceSection, stateRulesKey, Name(AppearanceStateRuleKey::NeutralHoverLighten)};
			rules.neutralHoverLighten = ReadValue(root, stateRulePath, {}, rules.neutralHoverLighten);

			stateRulePath[2] = Name(AppearanceStateRuleKey::NeutralActiveLighten);
			rules.neutralActiveLighten = ReadValue(root, stateRulePath, {}, rules.neutralActiveLighten);

			stateRulePath[2] = Name(AppearanceStateRuleKey::AccentHoverLighten);
			rules.accentHoverLighten = ReadValue(root, stateRulePath, {}, rules.accentHoverLighten);

			stateRulePath[2] = Name(AppearanceStateRuleKey::AccentHoverSaturation);
			rules.accentHoverSaturation = ReadValue(root, stateRulePath, {}, rules.accentHoverSaturation);

			stateRulePath[2] = Name(AppearanceStateRuleKey::AccentActiveDarken);
			rules.accentActiveDarken = ReadValue(root, stateRulePath, {}, rules.accentActiveDarken);

			stateRulePath[2] = Name(AppearanceStateRuleKey::AccentActiveSaturation);
			rules.accentActiveSaturation = ReadValue(root, stateRulePath, {}, rules.accentActiveSaturation);

			stateRulePath[2] = Name(AppearanceStateRuleKey::SelectedAlpha);
			rules.selectedAlpha = ReadValue(root, stateRulePath, {}, rules.selectedAlpha);

			stateRulePath[2] = Name(AppearanceStateRuleKey::SelectedHoverAlpha);
			rules.selectedHoverAlpha = ReadValue(root, stateRulePath, {}, rules.selectedHoverAlpha);

			stateRulePath[2] = Name(AppearanceStateRuleKey::SelectedActiveAlpha);
			rules.selectedActiveAlpha = ReadValue(root, stateRulePath, {}, rules.selectedActiveAlpha);

			stateRulePath[2] = Name(AppearanceStateRuleKey::DisabledAlpha);
			rules.disabledAlpha = ReadValue(root, stateRulePath, {}, rules.disabledAlpha);

			stateRulePath[2] = Name(AppearanceStateRuleKey::DisabledBackgroundMix);
			rules.disabledBackgroundMix = ReadValue(root, stateRulePath, {}, rules.disabledBackgroundMix);
		}

		void ApplyDefaultYaml(const YAML::Node &root, ApplicationConfig &config)
		{
			using namespace ConfigSchema;
			using Path2 = std::array<std::string_view, 2>;
			using Path3 = std::array<std::string_view, 3>;

			const char *logSection = Name(SectionKey::Log);
			const char *levelKey = Name(LogKey::Level);
			const char *toFileKey = Name(LogKey::ToFile);
			const char *filePathKey = Name(LogKey::FilePath);
			const char *traceEventsKey = Name(RootKey::TraceEvents);
			const char *windowSection = Name(SectionKey::Window);
			const char *xKey = Name(WindowKey::X);
			const char *yKey = Name(WindowKey::Y);
			const char *widthKey = Name(WindowKey::Width);
			const char *heightKey = Name(WindowKey::Height);
			const char *maximizedKey = Name(WindowKey::Maximized);
			const char *titleKey = Name(WindowKey::Title);
			const char *uiSection = Name(SectionKey::UI);
			const char *fontScaleKey = Name(UiKey::FontScale);
			const char *fontScaleMinKey = Name(UiKey::FontScaleMin);
			const char *fontScaleMaxKey = Name(UiKey::FontScaleMax);
			const char *fontScaleStepKey = Name(UiKey::FontScaleStep);
			const char *fontScaleStepMinKey = Name(UiKey::FontScaleStepMin);
			const char *fontScaleStepMaxKey = Name(UiKey::FontScaleStepMax);
			const char *fontScaleStepSliderMaxKey = Name(UiKey::FontScaleStepSliderMax);
			const char *settingsPreviewEnabledKey = Name(UiKey::SettingsPreviewEnabled);
			const char *settingsAutoSaveOnPreviewKey = Name(UiKey::SettingsAutoSaveOnPreview);
			const char *jobsSection = Name(SectionKey::Jobs);
			const char *defaultWorkerThreadsKey = Name(JobsKey::DefaultWorkerThreads);
			const char *reserveUrgentWorkerKey = Name(JobsKey::ReserveUrgentWorker);
			const char *eventQueueSection = Name(SectionKey::EventQueue);
			const char *initialCapacityKey = Name(EventQueueKey::InitialCapacity);
			const char *growthStepKey = Name(EventQueueKey::GrowthStep);

			Path2 logPath = {logSection, levelKey};
			config.log.level = ParseLogLevel(
				ReadString(root, logPath, Name(LegacyKey::LogLevel), LogLevelToString(config.log.level)),
				config.log.level);
			logPath[1] = toFileKey;
			config.log.toFile = ReadValue(root, logPath, Name(LegacyKey::LogToFile), config.log.toFile);
			logPath[1] = filePathKey;
			config.log.filePath = Path::FromResolved(
				ReadString(
					root,
					logPath,
					Name(LegacyKey::LogFilePath),
					config.log.filePath.Empty() ? RuntimeDefaults::LogFilePath : config.log.filePath.String()));
			config.log.traceEvents = ReadValue(root, {traceEventsKey}, traceEventsKey, config.log.traceEvents);

			Path2 windowPath = {windowSection, xKey};
			config.window.x = ReadValue(root, windowPath, Name(LegacyKey::WindowX), config.window.x);

			windowPath[1] = yKey;
			config.window.y = ReadValue(root, windowPath, Name(LegacyKey::WindowY), config.window.y);

			windowPath[1] = widthKey;
			config.window.width = ReadValue(root, windowPath, Name(LegacyKey::WindowWidth), config.window.width);

			windowPath[1] = heightKey;
			config.window.height = ReadValue(root, windowPath, Name(LegacyKey::WindowHeight), config.window.height);

			windowPath[1] = maximizedKey;
			config.window.maximized = ReadValue(root, windowPath, Name(LegacyKey::WindowMaximized), config.window.maximized);

			windowPath[1] = titleKey;
			config.window.title = ReadString(root, windowPath, Name(LegacyKey::WindowTitle), config.window.title);
			config.window.width = std::max(320, config.window.width);
			config.window.height = std::max(240, config.window.height);
			
			Path3 fontScalePath = {uiSection, fontScaleKey, fontScaleMinKey};
			config.ui.fontScaleMin = ReadValue(root, fontScalePath, Name(LegacyKey::UiFontScaleMin), config.ui.fontScaleMin);

			fontScalePath[2] = fontScaleMaxKey;
			config.ui.fontScaleMax = ReadValue(root, fontScalePath, Name(LegacyKey::UiFontScaleMax), config.ui.fontScaleMax);

			if (config.ui.fontScaleMax < config.ui.fontScaleMin)
				std::swap(config.ui.fontScaleMin, config.ui.fontScaleMax);

			Path3 fontScaleStepPath = {uiSection, fontScaleStepKey, fontScaleStepMinKey};
			config.ui.fontScaleStepMin = ReadValue(root, fontScaleStepPath, Name(LegacyKey::UiFontScaleStepMin), config.ui.fontScaleStepMin);

			fontScaleStepPath[2] = fontScaleStepMaxKey;
			config.ui.fontScaleStepMax = ReadValue(root, fontScaleStepPath, Name(LegacyKey::UiFontScaleStepMax), config.ui.fontScaleStepMax);

			if (config.ui.fontScaleStepMax < config.ui.fontScaleStepMin)
				std::swap(config.ui.fontScaleStepMin, config.ui.fontScaleStepMax);
			fontScaleStepPath[2] = fontScaleStepSliderMaxKey;
			config.ui.fontScaleStepSliderMax = ReadValue(
				root,
				fontScaleStepPath,
				Name(LegacyKey::UiFontScaleStepSliderMax),
				config.ui.fontScaleStepSliderMax);

			config.ui.fontScaleStepSliderMax = std::clamp(
				config.ui.fontScaleStepSliderMax,
				config.ui.fontScaleStepMin,
				config.ui.fontScaleStepMax);
			Path2 uiPath = {uiSection, settingsPreviewEnabledKey};
			config.ui.settingsPreviewEnabled = ReadValue(
				root,
				uiPath,
				Name(LegacyKey::UiSettingsPreviewEnabled),
				config.ui.settingsPreviewEnabled);

			uiPath[1] = settingsAutoSaveOnPreviewKey;
			config.ui.settingsAutoSaveOnPreview = ReadValue(
				root,
				uiPath,
				Name(LegacyKey::UiSettingsAutoSaveOnPreview),
				config.ui.settingsAutoSaveOnPreview);
			ApplyAppearanceYaml(root, config.appearance);

			Path2 jobsPath = {jobsSection, defaultWorkerThreadsKey};
			const int defaultWorkerThreadCount = ReadValue(
				root,
				jobsPath,
				Name(LegacyKey::JobsDefaultWorkerThreads),
				config.jobs.defaultWorkerThreadCount);
			config.jobs.defaultWorkerThreadCount = std::max(1, defaultWorkerThreadCount);

			jobsPath[1] = reserveUrgentWorkerKey;
			config.jobs.reserveUrgentWorker = ReadValue(
				root,
				jobsPath,
				Name(LegacyKey::JobsReserveUrgentWorker),
				config.jobs.reserveUrgentWorker);

			Path2 eventQueuePath = {eventQueueSection, initialCapacityKey};
			const int initialCapacity = ReadValue(
				root,
				eventQueuePath,
				Name(LegacyKey::EventQueueInitialCapacity),
				static_cast<int>(config.eventQueue.initialCapacity));

			eventQueuePath[1] = growthStepKey;
			const int growthStep = ReadValue(
				root,
				eventQueuePath,
				Name(LegacyKey::EventQueueGrowthStep),
				static_cast<int>(config.eventQueue.growthStep));
			
			config.eventQueue.initialCapacity = static_cast<std::size_t>(
				std::max(initialCapacity, static_cast<int>(RuntimeDefaults::EventQueueMinCapacity)));
			config.eventQueue.growthStep = static_cast<std::size_t>(
				std::max(growthStep, static_cast<int>(RuntimeDefaults::EventQueueMinGrowthStep)));
		}

		void ApplyUserYaml(const YAML::Node &root, ApplicationConfig &config)
		{
			using namespace ConfigSchema;
			using Path2 = std::array<std::string_view, 2>;
			using Path3 = std::array<std::string_view, 3>;

			const char *uiSection = Name(SectionKey::UI);
			const char *fontKey = Name(UiKey::Font);
			const char *fontPathKey = Name(UiKey::FontPath);
			const char *fontScaleKey = Name(UiKey::FontScale);
			const char *fontScaleStepKey = Name(UiKey::FontScaleStep);
			const char *windowSection = Name(SectionKey::Window);
			const char *xKey = Name(WindowKey::X);
			const char *yKey = Name(WindowKey::Y);
			const char *widthKey = Name(WindowKey::Width);
			const char *heightKey = Name(WindowKey::Height);
			const char *maximizedKey = Name(WindowKey::Maximized);
			const char *settingsPreviewEnabledKey = Name(UiKey::SettingsPreviewEnabled);
			const char *settingsAutoSaveOnPreviewKey = Name(UiKey::SettingsAutoSaveOnPreview);

			Path2 uiPath = {uiSection, fontScaleKey};
			config.ui.fontScale = ReadValue(root, uiPath, Name(LegacyKey::UiFontScale), config.ui.fontScale);
			uiPath[1] = fontScaleStepKey;
			config.ui.fontScaleStep = ReadValue(root, uiPath, Name(LegacyKey::UiFontScaleStep), config.ui.fontScaleStep);
			uiPath[1] = settingsPreviewEnabledKey;
			config.ui.settingsPreviewEnabled = ReadValue(
				root,
				uiPath,
				Name(LegacyKey::UiSettingsPreviewEnabled),
				config.ui.settingsPreviewEnabled);
			uiPath[1] = settingsAutoSaveOnPreviewKey;
			config.ui.settingsAutoSaveOnPreview = ReadValue(
				root,
				uiPath,
				Name(LegacyKey::UiSettingsAutoSaveOnPreview),
				config.ui.settingsAutoSaveOnPreview);

			Path2 windowPath = {windowSection, xKey};
			config.window.x = ReadValue(root, windowPath, Name(LegacyKey::WindowX), config.window.x);
			windowPath[1] = yKey;
			config.window.y = ReadValue(root, windowPath, Name(LegacyKey::WindowY), config.window.y);
			windowPath[1] = widthKey;
			config.window.width = ReadValue(root, windowPath, Name(LegacyKey::WindowWidth), config.window.width);
			windowPath[1] = heightKey;
			config.window.height = ReadValue(root, windowPath, Name(LegacyKey::WindowHeight), config.window.height);
			windowPath[1] = maximizedKey;
			config.window.maximized = ReadValue(root, windowPath, Name(LegacyKey::WindowMaximized), config.window.maximized);
			config.window.width = std::max(320, config.window.width);
			config.window.height = std::max(240, config.window.height);

			Path3 fontPath = {uiSection, fontKey, fontPathKey};
			config.ui.fontPath = ResolvePortablePathValue(
				config,
				ReadString(root, fontPath, Name(LegacyKey::UiFontPath), config.ui.fontPath));
			ApplyAppearanceYaml(root, config.appearance);

			config.ui.fontScale = std::clamp( config.ui.fontScale, config.ui.fontScaleMin, config.ui.fontScaleMax);
			config.ui.fontScaleStep = std::clamp( config.ui.fontScaleStep, config.ui.fontScaleStepMin, config.ui.fontScaleStepMax);
		}

		bool LoadYamlFile(const Path &path, YAML::Node &root, std::string &error)
		{
			try
			{
				root = YAML::LoadFile(path.String());
				if (!root || root.IsNull())
					root = YAML::Node(YAML::NodeType::Map);
				if (!root.IsMap())
				{
					error = "YAML config root must be a mapping: " + path.String();
					return false;
				}
				return true;
			}
			catch (const YAML::Exception &exception)
			{
				error = exception.what();
				return false;
			}
		}

		bool EnsureParentDirectory(const Path &path, std::string &error)
		{
			if (path.parent_path().empty())
				return true;

			std::error_code directoryError;
			FileSystem::CreateDirectories(path.parent_path().Native(), directoryError);
			if (!directoryError)
				return true;

			error = directoryError.message();
			return false;
		}

		bool WriteEmitterToFile(const Path &path, const YAML::Emitter &emitter, std::string &error)
		{
			if (!emitter.good())
			{
				error = "Unable to emit YAML config";
				return false;
			}

			if (!EnsureParentDirectory(path, error))
				return false;

			std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				error = "Unable to open YAML config for writing: " + path.String();
				return false;
			}

			stream << emitter.c_str() << '\n';
			return true;
		}

		std::filesystem::path NormalizePathForConfig(const std::filesystem::path &path)
		{
			std::error_code error;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
			if (error)
			{
				error.clear();
				normalized = std::filesystem::absolute(path, error);
			}
			if (error)
				normalized = path;

			return normalized.lexically_normal();
		}

		bool IsPathInsideBase(const std::filesystem::path &relativePath)
		{
			if (relativePath.empty() || relativePath.is_absolute())
				return false;

			for (const auto &part : relativePath)
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		std::string ToPortablePathValue(const ApplicationConfig &config, std::string_view value)
		{
			if (value.empty())
				return {};

			const std::filesystem::path rawPath{std::string(value)};
			if (!rawPath.is_absolute() || config.paths.installRoot.Empty())
				return rawPath.generic_string();

			const std::filesystem::path absolutePath = NormalizePathForConfig(rawPath);
			const std::filesystem::path installRoot = NormalizePathForConfig(config.paths.installRoot.Native());

			std::error_code error;
			const std::filesystem::path relativePath = std::filesystem::relative(absolutePath, installRoot, error);
			if (!error && IsPathInsideBase(relativePath))
				return relativePath.generic_string();

			return absolutePath.string();
		}

		std::string ResolvePortablePathValue(const ApplicationConfig &config, std::string_view value)
		{
			if (value.empty())
				return {};

			const std::filesystem::path rawPath{std::string(value)};
			if (rawPath.is_absolute())
				return NormalizePathForConfig(rawPath).string();

			std::vector<std::filesystem::path> bases;
			if (!config.paths.installRoot.Empty())
				bases.push_back(config.paths.installRoot.Native());
			if (!config.paths.appConfigDirectory.Empty())
				bases.push_back(config.paths.appConfigDirectory.Native().parent_path());
			bases.push_back(FileSystem::CurrentPath());

			for (const std::filesystem::path &base : bases)
			{
				const std::filesystem::path candidate = (base / rawPath).lexically_normal();
				std::error_code error;
				if (std::filesystem::exists(candidate, error) && !error)
					return NormalizePathForConfig(candidate).string();
			}

			return rawPath.generic_string();
		}

		void EmitColor(YAML::Emitter &emitter, const std::array<float, 4> &color)
		{
			emitter << YAML::Flow << YAML::BeginSeq;
			for (float component : color)
				emitter << component;
			emitter << YAML::EndSeq << YAML::Block;
		}

		void EmitVector2(YAML::Emitter &emitter, float x, float y)
		{
			using namespace ConfigSchema;

			const char *xKey = Name(AppearanceSpacingKey::X);
			const char *yKey = Name(AppearanceSpacingKey::Y);

			emitter << YAML::BeginMap;
			emitter << YAML::Key << xKey << YAML::Value << x;
			emitter << YAML::Key << yKey << YAML::Value << y;
			emitter << YAML::EndMap;
		}

		void EmitAppearance(YAML::Emitter &out, const AppearanceConfig &appearance)
		{
			using namespace ConfigSchema;

			const char *appearanceSection = Name(SectionKey::Appearance);
			const char *clearColorKey = Name(AppearanceKey::ClearColor);
			const char *colorsKey = Name(AppearanceKey::Colors);
			const char *roundingKey = Name(AppearanceKey::Rounding);
			const char *spacingKey = Name(AppearanceKey::Spacing);
			const char *stateRulesKey = Name(AppearanceKey::StateRules);
			const char *windowPaddingKey = Name(AppearanceSpacingKey::WindowPadding);
			const char *framePaddingKey = Name(AppearanceSpacingKey::FramePadding);
			const char *itemSpacingKey = Name(AppearanceSpacingKey::ItemSpacing);

			out << YAML::Key << appearanceSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << clearColorKey << YAML::Value;
			EmitColor(out, appearance.clearColor);

			out << YAML::Key << colorsKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << Name(AppearanceColorKey::Background) << YAML::Value;
			EmitColor(out, appearance.backgroundColor);
			out << YAML::Key << Name(AppearanceColorKey::Surface) << YAML::Value;
			EmitColor(out, appearance.surfaceColor);
			out << YAML::Key << Name(AppearanceColorKey::RaisedSurface) << YAML::Value;
			EmitColor(out, appearance.raisedSurfaceColor);
			out << YAML::Key << Name(AppearanceColorKey::Border) << YAML::Value;
			EmitColor(out, appearance.borderColor);
			out << YAML::Key << Name(AppearanceColorKey::Collapsible) << YAML::Value;
			EmitColor(out, appearance.collapsibleColor);
			out << YAML::Key << Name(AppearanceColorKey::Text) << YAML::Value;
			EmitColor(out, appearance.textColor);
			out << YAML::Key << Name(AppearanceColorKey::MutedText) << YAML::Value;
			EmitColor(out, appearance.mutedTextColor);
			out << YAML::Key << Name(AppearanceColorKey::Accent) << YAML::Value;
			EmitColor(out, appearance.accentColor);
			out << YAML::EndMap;

			out << YAML::Key << roundingKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << Name(AppearanceRoundingKey::Window) << YAML::Value << appearance.windowRounding;
			out << YAML::Key << Name(AppearanceRoundingKey::Frame) << YAML::Value << appearance.frameRounding;
			out << YAML::Key << Name(AppearanceRoundingKey::Grab) << YAML::Value << appearance.grabRounding;
			out << YAML::Key << Name(AppearanceRoundingKey::Popup) << YAML::Value << appearance.popupRounding;
			out << YAML::Key << Name(AppearanceRoundingKey::Scrollbar) << YAML::Value << appearance.scrollbarRounding;
			out << YAML::Key << Name(AppearanceRoundingKey::Tab) << YAML::Value << appearance.tabRounding;
			out << YAML::EndMap;

			out << YAML::Key << spacingKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << windowPaddingKey << YAML::Value;
			EmitVector2(out, appearance.windowPaddingX, appearance.windowPaddingY);
			out << YAML::Key << framePaddingKey << YAML::Value;
			EmitVector2(out, appearance.framePaddingX, appearance.framePaddingY);
			out << YAML::Key << itemSpacingKey << YAML::Value;
			EmitVector2(out, appearance.itemSpacingX, appearance.itemSpacingY);
			out << YAML::EndMap;

			const auto &rules = appearance.stateRules;
			out << YAML::Key << stateRulesKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << Name(AppearanceStateRuleKey::NeutralHoverLighten) << YAML::Value << rules.neutralHoverLighten;
			out << YAML::Key << Name(AppearanceStateRuleKey::NeutralActiveLighten) << YAML::Value << rules.neutralActiveLighten;
			out << YAML::Key << Name(AppearanceStateRuleKey::AccentHoverLighten) << YAML::Value << rules.accentHoverLighten;
			out << YAML::Key << Name(AppearanceStateRuleKey::AccentHoverSaturation) << YAML::Value << rules.accentHoverSaturation;
			out << YAML::Key << Name(AppearanceStateRuleKey::AccentActiveDarken) << YAML::Value << rules.accentActiveDarken;
			out << YAML::Key << Name(AppearanceStateRuleKey::AccentActiveSaturation) << YAML::Value << rules.accentActiveSaturation;
			out << YAML::Key << Name(AppearanceStateRuleKey::SelectedAlpha) << YAML::Value << rules.selectedAlpha;
			out << YAML::Key << Name(AppearanceStateRuleKey::SelectedHoverAlpha) << YAML::Value << rules.selectedHoverAlpha;
			out << YAML::Key << Name(AppearanceStateRuleKey::SelectedActiveAlpha) << YAML::Value << rules.selectedActiveAlpha;
			out << YAML::Key << Name(AppearanceStateRuleKey::DisabledAlpha) << YAML::Value << rules.disabledAlpha;
			out << YAML::Key << Name(AppearanceStateRuleKey::DisabledBackgroundMix) << YAML::Value << rules.disabledBackgroundMix;
			out << YAML::EndMap;
			out << YAML::EndMap;
		}

		bool WriteDefaultConfigFile(const Path &path, const ApplicationConfig &config, std::string &error)
		{
			using namespace ConfigSchema;

			const char *schemaVersionKey = Name(RootKey::SchemaVersion);
			const char *logSection = Name(SectionKey::Log);
			const char *levelKey = Name(LogKey::Level);
			const char *toFileKey = Name(LogKey::ToFile);
			const char *filePathKey = Name(LogKey::FilePath);
			const char *traceEventsKey = Name(RootKey::TraceEvents);
			const char *windowSection = Name(SectionKey::Window);
			const char *xKey = Name(WindowKey::X);
			const char *yKey = Name(WindowKey::Y);
			const char *widthKey = Name(WindowKey::Width);
			const char *heightKey = Name(WindowKey::Height);
			const char *maximizedKey = Name(WindowKey::Maximized);
			const char *titleKey = Name(WindowKey::Title);
			const char *uiSection = Name(SectionKey::UI);
			const char *fontScaleKey = Name(UiKey::FontScale);
			const char *fontScaleMinKey = Name(UiKey::FontScaleMin);
			const char *fontScaleMaxKey = Name(UiKey::FontScaleMax);
			const char *fontScaleStepKey = Name(UiKey::FontScaleStep);
			const char *fontScaleStepMinKey = Name(UiKey::FontScaleStepMin);
			const char *fontScaleStepMaxKey = Name(UiKey::FontScaleStepMax);
			const char *fontScaleStepSliderMaxKey = Name(UiKey::FontScaleStepSliderMax);
			const char *settingsPreviewEnabledKey = Name(UiKey::SettingsPreviewEnabled);
			const char *settingsAutoSaveOnPreviewKey = Name(UiKey::SettingsAutoSaveOnPreview);
			const char *jobsSection = Name(SectionKey::Jobs);
			const char *defaultWorkerThreadsKey = Name(JobsKey::DefaultWorkerThreads);
			const char *reserveUrgentWorkerKey = Name(JobsKey::ReserveUrgentWorker);
			const char *eventQueueSection = Name(SectionKey::EventQueue);
			const char *initialCapacityKey = Name(EventQueueKey::InitialCapacity);
			const char *growthStepKey = Name(EventQueueKey::GrowthStep);

			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << schemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << logSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << levelKey << YAML::Value << LogLevelToString(config.log.level);
			out << YAML::Key << toFileKey << YAML::Value << config.log.toFile;
			out << YAML::Key << filePathKey << YAML::Value << config.log.filePath.String();
			out << YAML::EndMap;
			out << YAML::Key << traceEventsKey << YAML::Value << config.log.traceEvents;

			out << YAML::Key << windowSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << xKey << YAML::Value << config.window.x;
			out << YAML::Key << yKey << YAML::Value << config.window.y;
			out << YAML::Key << widthKey << YAML::Value << config.window.width;
			out << YAML::Key << heightKey << YAML::Value << config.window.height;
			out << YAML::Key << maximizedKey << YAML::Value << config.window.maximized;
			out << YAML::Key << titleKey << YAML::Value << config.window.title;
			out << YAML::EndMap;

			out << YAML::Key << uiSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontScaleKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontScaleMinKey << YAML::Value << config.ui.fontScaleMin;
			out << YAML::Key << fontScaleMaxKey << YAML::Value << config.ui.fontScaleMax;
			out << YAML::EndMap;
			out << YAML::Key << fontScaleStepKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontScaleStepMinKey << YAML::Value << config.ui.fontScaleStepMin;
			out << YAML::Key << fontScaleStepMaxKey << YAML::Value << config.ui.fontScaleStepMax;
			out << YAML::Key << fontScaleStepSliderMaxKey << YAML::Value << config.ui.fontScaleStepSliderMax;
			out << YAML::EndMap;
			out << YAML::Key << settingsPreviewEnabledKey << YAML::Value << config.ui.settingsPreviewEnabled;
			out << YAML::Key << settingsAutoSaveOnPreviewKey << YAML::Value << config.ui.settingsAutoSaveOnPreview;
			out << YAML::EndMap;

			EmitAppearance(out, config.appearance);

			out << YAML::Key << jobsSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << defaultWorkerThreadsKey << YAML::Value << config.jobs.defaultWorkerThreadCount;
			out << YAML::Key << reserveUrgentWorkerKey << YAML::Value << config.jobs.reserveUrgentWorker;
			out << YAML::EndMap;

			out << YAML::Key << eventQueueSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << initialCapacityKey << YAML::Value << static_cast<unsigned long long>(config.eventQueue.initialCapacity);
			out << YAML::Key << growthStepKey << YAML::Value << static_cast<unsigned long long>(config.eventQueue.growthStep);
			out << YAML::EndMap;
			out << YAML::EndMap;

			return WriteEmitterToFile(path, out, error);
		}

		bool WriteUserSettingsFile(const Path &path, const ApplicationConfig &config, std::string &error)
		{
			using namespace ConfigSchema;

			const char *schemaVersionKey = Name(RootKey::SchemaVersion);
			const char *uiSection = Name(SectionKey::UI);
			const char *windowSection = Name(SectionKey::Window);
			const char *xKey = Name(WindowKey::X);
			const char *yKey = Name(WindowKey::Y);
			const char *widthKey = Name(WindowKey::Width);
			const char *heightKey = Name(WindowKey::Height);
			const char *maximizedKey = Name(WindowKey::Maximized);
			const char *fontKey = Name(UiKey::Font);
			const char *fontPathKey = Name(UiKey::FontPath);
			const char *fontScaleKey = Name(UiKey::FontScale);
			const char *fontScaleStepKey = Name(UiKey::FontScaleStep);
			const char *settingsPreviewEnabledKey = Name(UiKey::SettingsPreviewEnabled);
			const char *settingsAutoSaveOnPreviewKey = Name(UiKey::SettingsAutoSaveOnPreview);

			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << schemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << uiSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontPathKey << YAML::Value << ToPortablePathValue(config, config.ui.fontPath);
			out << YAML::EndMap;
			out << YAML::Key << fontScaleKey << YAML::Value << config.ui.fontScale;
			out << YAML::Key << fontScaleStepKey << YAML::Value << config.ui.fontScaleStep;
			out << YAML::Key << settingsPreviewEnabledKey << YAML::Value << config.ui.settingsPreviewEnabled;
			out << YAML::Key << settingsAutoSaveOnPreviewKey << YAML::Value << config.ui.settingsAutoSaveOnPreview;
			out << YAML::EndMap;
			out << YAML::Key << windowSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << xKey << YAML::Value << config.window.x;
			out << YAML::Key << yKey << YAML::Value << config.window.y;
			out << YAML::Key << widthKey << YAML::Value << config.window.width;
			out << YAML::Key << heightKey << YAML::Value << config.window.height;
			out << YAML::Key << maximizedKey << YAML::Value << config.window.maximized;
			out << YAML::EndMap;
			EmitAppearance(out, config.appearance);
			out << YAML::EndMap;

			return WriteEmitterToFile(path, out, error);
		}

		bool WriteProfileFile(const Path &path, const ApplicationConfig &config, std::string &error)
		{
			using namespace ConfigSchema;

			const char *schemaVersionKey = Name(RootKey::SchemaVersion);
			const char *logSection = Name(SectionKey::Log);
			const char *levelKey = Name(LogKey::Level);
			const char *toFileKey = Name(LogKey::ToFile);
			const char *filePathKey = Name(LogKey::FilePath);
			const char *traceEventsKey = Name(RootKey::TraceEvents);
			const char *windowSection = Name(SectionKey::Window);
			const char *xKey = Name(WindowKey::X);
			const char *yKey = Name(WindowKey::Y);
			const char *widthKey = Name(WindowKey::Width);
			const char *heightKey = Name(WindowKey::Height);
			const char *maximizedKey = Name(WindowKey::Maximized);
			const char *titleKey = Name(WindowKey::Title);
			const char *uiSection = Name(SectionKey::UI);
			const char *fontKey = Name(UiKey::Font);
			const char *fontPathKey = Name(UiKey::FontPath);
			const char *fontScaleKey = Name(UiKey::FontScale);
			const char *fontScaleStepKey = Name(UiKey::FontScaleStep);
			const char *settingsPreviewEnabledKey = Name(UiKey::SettingsPreviewEnabled);
			const char *settingsAutoSaveOnPreviewKey = Name(UiKey::SettingsAutoSaveOnPreview);
			const char *jobsSection = Name(SectionKey::Jobs);
			const char *defaultWorkerThreadsKey = Name(JobsKey::DefaultWorkerThreads);
			const char *reserveUrgentWorkerKey = Name(JobsKey::ReserveUrgentWorker);
			const char *eventQueueSection = Name(SectionKey::EventQueue);
			const char *initialCapacityKey = Name(EventQueueKey::InitialCapacity);
			const char *growthStepKey = Name(EventQueueKey::GrowthStep);

			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << schemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << logSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << levelKey << YAML::Value << LogLevelToString(config.log.level);
			out << YAML::Key << toFileKey << YAML::Value << config.log.toFile;
			out << YAML::Key << filePathKey << YAML::Value << config.log.filePath.String();
			out << YAML::EndMap;
			out << YAML::Key << traceEventsKey << YAML::Value << config.log.traceEvents;

			out << YAML::Key << windowSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << xKey << YAML::Value << config.window.x;
			out << YAML::Key << yKey << YAML::Value << config.window.y;
			out << YAML::Key << widthKey << YAML::Value << config.window.width;
			out << YAML::Key << heightKey << YAML::Value << config.window.height;
			out << YAML::Key << maximizedKey << YAML::Value << config.window.maximized;
			out << YAML::Key << titleKey << YAML::Value << config.window.title;
			out << YAML::EndMap;

			out << YAML::Key << uiSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontKey << YAML::Value << YAML::BeginMap;
			out << YAML::Key << fontPathKey << YAML::Value << ToPortablePathValue(config, config.ui.fontPath);
			out << YAML::EndMap;
			out << YAML::Key << fontScaleKey << YAML::Value << config.ui.fontScale;
			out << YAML::Key << fontScaleStepKey << YAML::Value << config.ui.fontScaleStep;
			out << YAML::Key << settingsPreviewEnabledKey << YAML::Value << config.ui.settingsPreviewEnabled;
			out << YAML::Key << settingsAutoSaveOnPreviewKey << YAML::Value << config.ui.settingsAutoSaveOnPreview;
			out << YAML::EndMap;

			EmitAppearance(out, config.appearance);

			out << YAML::Key << jobsSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << defaultWorkerThreadsKey << YAML::Value << config.jobs.defaultWorkerThreadCount;
			out << YAML::Key << reserveUrgentWorkerKey << YAML::Value << config.jobs.reserveUrgentWorker;
			out << YAML::EndMap;

			out << YAML::Key << eventQueueSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << initialCapacityKey << YAML::Value << static_cast<unsigned long long>(config.eventQueue.initialCapacity);
			out << YAML::Key << growthStepKey << YAML::Value << static_cast<unsigned long long>(config.eventQueue.growthStep);
			out << YAML::EndMap;
			out << YAML::EndMap;

			return WriteEmitterToFile(path, out, error);
		}
	}

	namespace ConfigPaths
	{
		Path NormalizeConfigDirectory(const Path &configDirectory)
		{
			if (!configDirectory.Empty())
				return Path::FromResolved(configDirectory.Native());

			return Path::FromResolved(FileSystem::CurrentPath() / "install" / "app" / "config");
		}

		bool IsPortableAppConfigDirectory(const Path &configDirectory)
		{
			const std::filesystem::path path = configDirectory.Native().lexically_normal();
			return path.filename() == "config"
				&& path.parent_path().filename() == "app"
				&& path.parent_path().parent_path().filename() == "install";
		}

		ApplicationPaths Build(const Path &configDirectory)
		{
			ApplicationPaths paths;
			paths.appConfigDirectory = NormalizeConfigDirectory(configDirectory);

			if (IsPortableAppConfigDirectory(paths.appConfigDirectory))
			{
				paths.installRoot = Path::FromResolved(paths.appConfigDirectory.Native().parent_path().parent_path());
				const Path appRoot = Path::FromResolved(paths.installRoot.Native() / "app");
				const Path userRoot = Path::FromResolved(paths.installRoot.Native() / "users" / "default");
				paths.userConfigDirectory = Path::FromResolved(userRoot.Native() / "config");
				paths.profilesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "profiles");
				paths.themesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "themes");
				paths.layoutsDirectory = Path::FromResolved(userRoot.Native() / "layouts");
				paths.exportsDirectory = Path::FromResolved(userRoot.Native() / "exports");
				paths.assetsDirectory = Path::FromResolved(appRoot.Native() / "assets");
				paths.fontsDirectory = Path::FromResolved(paths.assetsDirectory.Native() / "fonts");
				return paths;
			}

			paths.installRoot = Path::FromResolved(paths.appConfigDirectory.Native().parent_path());
			paths.userConfigDirectory = paths.appConfigDirectory;
			paths.profilesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "profiles");
			paths.themesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "themes");
			paths.layoutsDirectory = paths.userConfigDirectory;
			paths.exportsDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "exports");
			paths.assetsDirectory = Path::FromResolved(paths.installRoot.Native() / "assets");
			paths.fontsDirectory = Path::FromResolved(paths.assetsDirectory.Native() / "fonts");
			return paths;
		}

		bool EnsureDirectory(const Path &directory, std::string &error)
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(directory.Native(), directoryError);
			if (!directoryError)
				return true;

			error = directoryError.message();
			return false;
		}

		bool EnsurePortableDirectories(const ApplicationPaths &paths, std::string &error)
		{
			return EnsureDirectory(paths.appConfigDirectory, error)
				&& EnsureDirectory(paths.userConfigDirectory, error)
				&& EnsureDirectory(paths.profilesDirectory, error)
				&& EnsureDirectory(paths.themesDirectory, error)
				&& EnsureDirectory(paths.layoutsDirectory, error)
				&& EnsureDirectory(paths.exportsDirectory, error)
				&& EnsureDirectory(paths.assetsDirectory, error)
				&& EnsureDirectory(paths.fontsDirectory, error);
		}

		std::vector<Path> ListFilesByExtension(const Path &directory, std::string_view extension)
		{
			std::vector<Path> result;
			std::error_code error;
			if (!std::filesystem::exists(directory.Native(), error) || error)
				return result;

			for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
			{
				if (error)
					break;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				std::string fileExtension = entry.path().extension().string();
				std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), [](unsigned char character) {
					return static_cast<char>(std::tolower(character));
				});
				if (fileExtension == extension)
					result.push_back(Path::FromResolved(entry.path()));
			}

			std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
				return left.String() < right.String();
			});
			return result;
		}
	}

	ConfigManager::ConfigManager(Path configDirectory):
		m_ConfigDirectory(std::move(configDirectory)),
		m_Config(CreateDefaultConfig(m_ConfigDirectory))
	{
	}

	bool ConfigManager::Initialize(std::string &error)
	{
		m_ConfigDirectory = ConfigPaths::NormalizeConfigDirectory(m_ConfigDirectory);
		m_Config = CreateDefaultConfig(m_ConfigDirectory);
		if (!ConfigPaths::EnsurePortableDirectories(m_Config.paths, error))
			return false;

		if (!loadOrCreateDefaultConfig(error))
			return false;
		if (!loadOrCreateUserSettings(error))
			return false;

		m_Config.directory = m_ConfigDirectory;
		m_Config.paths = ResolvePortablePaths(m_ConfigDirectory);
		m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).String();
		m_Initialized = true;
		return true;
	}

	const Path &ConfigManager::GetConfigDirectory() const
	{
		return m_ConfigDirectory;
	}

	const ApplicationConfig &ConfigManager::GetConfig() const
	{
		return m_Config;
	}

	const ApplicationPaths &ConfigManager::GetPaths() const
	{
		return m_Config.paths;
	}

	void ConfigManager::SetConfig(const ApplicationConfig &config)
	{
		m_Config = config;
		m_Config.directory = m_ConfigDirectory;
		m_Config.paths = ResolvePortablePaths(m_ConfigDirectory);
		if (m_Config.layout.imGuiIniPath.empty())
			m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).String();
	}

	void ConfigManager::SetUiConfig(const UIConfig &config)
	{
		m_Config.ui = config;
	}

	void ConfigManager::SetAppearanceConfig(const AppearanceConfig &config)
	{
		m_Config.appearance = config;
	}

	bool ConfigManager::SaveDefaultConfig(std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		return ConfigYaml::WriteDefaultConfigFile(GetDefaultConfigPath(m_ConfigDirectory), m_Config, error);
	}

	bool ConfigManager::SaveUserSettings(std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		return ConfigYaml::WriteUserSettingsFile(GetUserSettingsPath(m_ConfigDirectory), m_Config, error);
	}

	bool ConfigManager::SaveAppearanceTheme(const Path &path, const AppearanceConfig &appearance, std::string &error) const
	{
		using namespace ConfigSchema;
		const char *schemaVersionKey = Name(RootKey::SchemaVersion);

		if (!ensureInitialized(error))
			return false;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << schemaVersionKey << YAML::Value << SchemaVersion;
		ConfigYaml::EmitAppearance(out, appearance);
		out << YAML::EndMap;
		return ConfigYaml::WriteEmitterToFile(path, out, error);
	}

	bool ConfigManager::LoadAppearanceTheme(const Path &path, AppearanceConfig &appearance, std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		YAML::Node root;
		if (!ConfigYaml::LoadYamlFile(path, root, error))
			return false;

		ConfigYaml::ApplyAppearanceYaml(root, appearance);
		return true;
	}

	bool ConfigManager::SaveConfigProfile(const Path &path, const ApplicationConfig &config, std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		return ConfigYaml::WriteProfileFile(path, config, error);
	}

	bool ConfigManager::LoadConfigProfile(const Path &path, ApplicationConfig &config, std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		YAML::Node root;
		if (!ConfigYaml::LoadYamlFile(path, root, error))
			return false;

		config = CreateDefaultConfig(m_ConfigDirectory);
		ConfigYaml::ApplyDefaultYaml(root, config);
		ConfigYaml::ApplyUserYaml(root, config);
		config.directory = m_ConfigDirectory;
		config.paths = ResolvePortablePaths(m_ConfigDirectory);
		config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).String();
		return true;
	}

	bool ConfigManager::SaveTextFile(const Path &path, std::string_view text, std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;
		if (!ConfigYaml::EnsureParentDirectory(path, error))
			return false;

		std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "Unable to open file for writing: " + path.String();
			return false;
		}

		stream.write(text.data(), static_cast<std::streamsize>(text.size()));
		return stream.good();
	}

	bool ConfigManager::LoadTextFile(const Path &path, std::string &text, std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		std::ifstream stream(path.Native(), std::ios::binary);
		if (!stream)
		{
			error = "Unable to open file for reading: " + path.String();
			return false;
		}

		text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return true;
	}

	std::vector<Path> ConfigManager::ListYamlFiles(const Path &directory) const
	{
		std::vector<Path> result = ConfigPaths::ListFilesByExtension(directory, ".yaml");
		std::vector<Path> ymlFiles = ConfigPaths::ListFilesByExtension(directory, ".yml");
		result.insert(result.end(), ymlFiles.begin(), ymlFiles.end());
		std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
			return left.String() < right.String();
		});
		return result;
	}

	std::vector<Path> ConfigManager::ListIniFiles(const Path &directory) const
	{
		return ConfigPaths::ListFilesByExtension(directory, ".ini");
	}

	void ConfigManager::ApplySpecification(ApplicationSpecification &specification) const
	{
		specification.logLevel = m_Config.log.level;
		specification.logToFile = m_Config.log.toFile;
		specification.logFilePath = m_Config.log.filePath;
		specification.traceEvents = m_Config.log.traceEvents;
	}

	ApplicationConfig ConfigManager::CreateDefaultConfig(const Path &configDirectory)
	{
		ApplicationConfig config;
		config.directory = ConfigPaths::NormalizeConfigDirectory(configDirectory);
		config.paths = ResolvePortablePaths(config.directory);
		config.layout.imGuiIniPath = GetLayoutPath(configDirectory).String();
		config.log.level = LogLevel::Info;
		config.log.toFile = true;
		config.log.filePath = Path::FromResolved(RuntimeDefaults::LogFilePath);
		config.log.traceEvents = false;
		config.window.x = RuntimeDefaults::WindowPosX;
		config.window.y = RuntimeDefaults::WindowPosY;
		config.window.width = RuntimeDefaults::WindowWidth;
		config.window.height = RuntimeDefaults::WindowHeight;
		config.window.maximized = RuntimeDefaults::WindowMaximized;
		config.window.title = RuntimeDefaults::WindowTitle;
		config.ui.fontScale = RuntimeDefaults::UiFontScale;
		config.ui.fontScaleStep = RuntimeDefaults::UiFontScaleStep;
		config.ui.fontPath = RuntimeDefaults::UiFontPath;
		config.ui.settingsPreviewEnabled = RuntimeDefaults::UiSettingsPreviewEnabled;
		config.ui.settingsAutoSaveOnPreview = RuntimeDefaults::UiSettingsAutoSaveOnPreview;
		config.ui.fontScaleMin = RuntimeDefaults::UiFontScaleMin;
		config.ui.fontScaleMax = RuntimeDefaults::UiFontScaleMax;
		config.ui.fontScaleStepMin = RuntimeDefaults::UiFontScaleStepMin;
		config.ui.fontScaleStepMax = RuntimeDefaults::UiFontScaleStepMax;
		config.ui.fontScaleStepSliderMax = RuntimeDefaults::UiFontScaleStepSliderMax;
		config.jobs.defaultWorkerThreadCount = RuntimeDefaults::JobsDefaultWorkerThreads;
		config.jobs.reserveUrgentWorker = RuntimeDefaults::JobsReserveUrgentWorker;
		config.eventQueue.initialCapacity = RuntimeDefaults::EventQueueInitialCapacity;
		config.eventQueue.growthStep = RuntimeDefaults::EventQueueGrowthStep;
		return config;
	}

	ApplicationPaths ConfigManager::ResolvePortablePaths(const Path &configDirectory)
	{
		return ConfigPaths::Build(configDirectory);
	}

	Path ConfigManager::GetDefaultConfigPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).appConfigDirectory / Path(DefaultConfigFileName);
	}

	Path ConfigManager::GetUserSettingsPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).userConfigDirectory / Path(UserSettingsFileName);
	}

	Path ConfigManager::GetLayoutPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).layoutsDirectory / Path(LayoutFileName);
	}

	Path ConfigManager::GetProfilesDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).profilesDirectory;
	}

	Path ConfigManager::GetThemesDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).themesDirectory;
	}

	Path ConfigManager::GetLayoutsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).layoutsDirectory;
	}

	Path ConfigManager::GetExportsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).exportsDirectory;
	}

	Path ConfigManager::GetAssetsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).assetsDirectory;
	}

	Path ConfigManager::GetFontsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).fontsDirectory;
	}

	bool ConfigManager::ensureInitialized(std::string &error) const
	{
		if (m_Initialized)
			return true;

		error = "ConfigManager is not initialized";
		return false;
	}

	bool ConfigManager::loadOrCreateDefaultConfig(std::string &error)
	{
		const Path path = GetDefaultConfigPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
			return ConfigYaml::WriteDefaultConfigFile(path, m_Config, error);

		YAML::Node root;
		if (!ConfigYaml::LoadYamlFile(path, root, error))
			return false;

		ConfigYaml::ApplyDefaultYaml(root, m_Config);
		return true;
	}

	bool ConfigManager::loadOrCreateUserSettings(std::string &error)
	{
		const Path path = GetUserSettingsPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
		{
			const Path legacyAppSettingsPath = m_Config.paths.appConfigDirectory / Path(UserSettingsFileName);
			if (legacyAppSettingsPath != path && FileSystem::Exists(legacyAppSettingsPath.Native()))
			{
				YAML::Node legacyRoot;
				if (!ConfigYaml::LoadYamlFile(legacyAppSettingsPath, legacyRoot, error))
					return false;
				ConfigYaml::ApplyUserYaml(legacyRoot, m_Config);
			}

			return ConfigYaml::WriteUserSettingsFile(path, m_Config, error);
		}

		YAML::Node root;
		if (!ConfigYaml::LoadYamlFile(path, root, error))
			return false;

		ConfigYaml::ApplyUserYaml(root, m_Config);
		return true;
	}
} // namespace DefectStudio
