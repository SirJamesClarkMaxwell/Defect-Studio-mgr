#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "App/ApplicationState.hpp"
#include "App/Serialization/IYamlCodec.hpp"

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

	class YamlConfigSerializer final : public IYamlCodec
	{
	public:
		YamlConfigSerializer() = default;

		[[nodiscard]] bool SerializeDefaultConfig(
			const ApplicationConfig &config,
			std::string &text,
			std::string &error) const override;
		[[nodiscard]] bool DeserializeDefaultConfig(
			std::string_view text,
			ApplicationConfig &config,
			std::string &error) const override;

		[[nodiscard]] bool SerializeUserSettings(
			const ApplicationConfig &config,
			std::string &text,
			std::string &error) const override;
		[[nodiscard]] bool DeserializeUserSettings(
			std::string_view text,
			ApplicationConfig &config,
			std::string &error) const override;

		[[nodiscard]] bool SerializeConfigProfile(
			const ApplicationConfig &config,
			std::string &text,
			std::string &error) const override;
		[[nodiscard]] bool DeserializeConfigProfile(
			std::string_view text,
			ApplicationConfig &config,
			std::string &error) const override;

		[[nodiscard]] bool SerializeAppearanceTheme(
			const AppearanceConfig &appearance,
			std::string &text,
			std::string &error) const override;
		[[nodiscard]] bool DeserializeAppearanceTheme(
			std::string_view text,
			AppearanceConfig &appearance,
			std::string &error) const override;
	};
} // namespace DefectStudio
