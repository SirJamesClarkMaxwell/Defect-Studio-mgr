#include "Core/dspch.hpp"

#include "App/Serialization/YamlConfigSerializer.hpp"

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

#include "App/Managers/ConfigManager.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/RuntimeTuning.hpp"

namespace DefectStudio
{

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

		FilePath NormalizePathForConfig(const FilePath &path)
		{
			std::error_code error;
			FilePath normalized = FileSystem::WeaklyCanonical(path, error);
			if (error)
			{
				error.clear();
				normalized = FileSystem::Absolute(path, error);
			}
			if (error)
				normalized = path;

			return normalized;
		}

		bool IsPathInsideBase(const FilePath &relativePath)
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

			const FilePath rawPath{std::string(value)};
			if (!rawPath.is_absolute() || config.paths.installRoot.Empty())
				return rawPath.generic_string();

			const FilePath absolutePath = NormalizePathForConfig(rawPath);
			const FilePath installRoot = NormalizePathForConfig(config.paths.installRoot.Native());

			std::error_code error;
			const FilePath relativePath = std::filesystem::relative(absolutePath, installRoot, error);
			if (!error && IsPathInsideBase(relativePath))
				return relativePath.generic_string();

			return absolutePath.string();
		}

		std::string ResolvePortablePathValue(const ApplicationConfig &config, std::string_view value)
		{
			if (value.empty())
				return {};

			const FilePath rawPath{std::string(value)};
			if (rawPath.is_absolute())
				return NormalizePathForConfig(rawPath).string();

			std::vector<FilePath> bases;
			if (!config.paths.installRoot.Empty())
				bases.push_back(config.paths.installRoot.Native());
			if (!config.paths.appConfigDirectory.Empty())
				bases.push_back(config.paths.appConfigDirectory.Native().parent_path());
			bases.push_back(FileSystem::CurrentPath());

			for (const FilePath &base : bases)
			{
				const FilePath candidate = FilePath((base / rawPath).lexically_normal());
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


	namespace ConfigYaml
	{
		void ApplyDefaultYaml(const YAML::Node &root, ApplicationConfig &config);
		void ApplyUserYaml(const YAML::Node &root, ApplicationConfig &config);
		void ApplyAppearanceYaml(const YAML::Node &root, AppearanceConfig &appearance);
	}

	namespace
	{
		constexpr const char *SchemaVersionKey = "schema_version";
		constexpr const char *TraceEventsKey = "trace_events";
		constexpr const char *AppearanceSection = "appearance";
		constexpr const char *EventQueueSection = "event_queue";
		constexpr const char *JobsSection = "jobs";
		constexpr const char *LogSection = "log";
		constexpr const char *UiSection = "ui";
		constexpr const char *WindowSection = "window";

		FilePath normalizePathForConfig(const FilePath &path)
		{
			std::error_code error;
			FilePath normalized = FileSystem::WeaklyCanonical(path, error);
			if (error)
			{
				error.clear();
				normalized = FileSystem::Absolute(path, error);
			}
			if (error)
				normalized = path;

			return normalized;
		}

		bool isPathInsideBase(const FilePath &relativePath)
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

		std::string toPortablePathValue(const ApplicationConfig &config, std::string_view value)
		{
			if (value.empty())
				return {};

			const FilePath rawPath{std::string(value)};
			if (!rawPath.is_absolute() || config.paths.installRoot.Empty())
				return rawPath.generic_string();

			const FilePath absolutePath = normalizePathForConfig(rawPath);
			const FilePath installRoot = normalizePathForConfig(config.paths.installRoot.Native());

			std::error_code error;
			const FilePath relativePath = std::filesystem::relative(absolutePath, installRoot, error);
			if (!error && isPathInsideBase(relativePath))
				return relativePath.generic_string();

			return absolutePath.string();
		}

		void emitColor(YAML::Emitter &emitter, const std::array<float, 4> &color)
		{
			emitter << YAML::Flow << YAML::BeginSeq;
			for (float component : color)
				emitter << component;
			emitter << YAML::EndSeq << YAML::Block;
		}

		void emitVector2(YAML::Emitter &emitter, float x, float y)
		{
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "x" << YAML::Value << x;
			emitter << YAML::Key << "y" << YAML::Value << y;
			emitter << YAML::EndMap;
		}

		void emitAppearance(YAML::Emitter &out, const AppearanceConfig &appearance)
		{
			out << YAML::Key << AppearanceSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "clear_color" << YAML::Value;
			emitColor(out, appearance.clearColor);

			out << YAML::Key << "colors" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "background" << YAML::Value;
			emitColor(out, appearance.backgroundColor);
			out << YAML::Key << "surface" << YAML::Value;
			emitColor(out, appearance.surfaceColor);
			out << YAML::Key << "raised_surface" << YAML::Value;
			emitColor(out, appearance.raisedSurfaceColor);
			out << YAML::Key << "border" << YAML::Value;
			emitColor(out, appearance.borderColor);
			out << YAML::Key << "collapsible" << YAML::Value;
			emitColor(out, appearance.collapsibleColor);
			out << YAML::Key << "text" << YAML::Value;
			emitColor(out, appearance.textColor);
			out << YAML::Key << "muted_text" << YAML::Value;
			emitColor(out, appearance.mutedTextColor);
			out << YAML::Key << "accent" << YAML::Value;
			emitColor(out, appearance.accentColor);
			out << YAML::EndMap;

			out << YAML::Key << "rounding" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "window" << YAML::Value << appearance.windowRounding;
			out << YAML::Key << "frame" << YAML::Value << appearance.frameRounding;
			out << YAML::Key << "grab" << YAML::Value << appearance.grabRounding;
			out << YAML::Key << "popup" << YAML::Value << appearance.popupRounding;
			out << YAML::Key << "scrollbar" << YAML::Value << appearance.scrollbarRounding;
			out << YAML::Key << "tab" << YAML::Value << appearance.tabRounding;
			out << YAML::EndMap;

			out << YAML::Key << "spacing" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "window_padding" << YAML::Value;
			emitVector2(out, appearance.windowPaddingX, appearance.windowPaddingY);
			out << YAML::Key << "frame_padding" << YAML::Value;
			emitVector2(out, appearance.framePaddingX, appearance.framePaddingY);
			out << YAML::Key << "item_spacing" << YAML::Value;
			emitVector2(out, appearance.itemSpacingX, appearance.itemSpacingY);
			out << YAML::EndMap;

			const AppearanceStateRules &rules = appearance.stateRules;
			out << YAML::Key << "state_rules" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "neutral_hover_lighten" << YAML::Value << rules.neutralHoverLighten;
			out << YAML::Key << "neutral_active_lighten" << YAML::Value << rules.neutralActiveLighten;
			out << YAML::Key << "accent_hover_lighten" << YAML::Value << rules.accentHoverLighten;
			out << YAML::Key << "accent_hover_saturation" << YAML::Value << rules.accentHoverSaturation;
			out << YAML::Key << "accent_active_darken" << YAML::Value << rules.accentActiveDarken;
			out << YAML::Key << "accent_active_saturation" << YAML::Value << rules.accentActiveSaturation;
			out << YAML::Key << "selected_alpha" << YAML::Value << rules.selectedAlpha;
			out << YAML::Key << "selected_hover_alpha" << YAML::Value << rules.selectedHoverAlpha;
			out << YAML::Key << "selected_active_alpha" << YAML::Value << rules.selectedActiveAlpha;
			out << YAML::Key << "disabled_alpha" << YAML::Value << rules.disabledAlpha;
			out << YAML::Key << "disabled_background_mix" << YAML::Value << rules.disabledBackgroundMix;
			out << YAML::EndMap;
			out << YAML::EndMap;
		}

		bool emitterToString(const YAML::Emitter &emitter, std::string &text, std::string &error)
		{
			if (!emitter.good())
			{
				error = "Unable to emit YAML config";
				return false;
			}

			text = emitter.c_str();
			text.push_back('\n');
			return true;
		}

		bool loadYamlText(std::string_view text, YAML::Node &root, std::string &error)
		{
			try
			{
				root = YAML::Load(std::string(text));
				if (!root || root.IsNull())
					root = YAML::Node(YAML::NodeType::Map);
				if (!root.IsMap())
				{
					error = "YAML config root must be a mapping";
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

		void emitDefaultConfig(YAML::Emitter &out, const ApplicationConfig &config)
		{
			out << YAML::BeginMap;
			out << YAML::Key << SchemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << LogSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "level" << YAML::Value << ToString(config.log.level);
			out << YAML::Key << "to_file" << YAML::Value << config.log.toFile;
			out << YAML::Key << "file_path" << YAML::Value << config.log.filePath.String();
			out << YAML::EndMap;
			out << YAML::Key << TraceEventsKey << YAML::Value << config.log.traceEvents;

			out << YAML::Key << WindowSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "x" << YAML::Value << config.window.x;
			out << YAML::Key << "y" << YAML::Value << config.window.y;
			out << YAML::Key << "width" << YAML::Value << config.window.width;
			out << YAML::Key << "height" << YAML::Value << config.window.height;
			out << YAML::Key << "maximized" << YAML::Value << config.window.maximized;
			out << YAML::Key << "title" << YAML::Value << config.window.title;
			out << YAML::EndMap;

			out << YAML::Key << UiSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "font_scale" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "min" << YAML::Value << config.ui.fontScaleMin;
			out << YAML::Key << "max" << YAML::Value << config.ui.fontScaleMax;
			out << YAML::EndMap;
			out << YAML::Key << "font_scale_step" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "min" << YAML::Value << config.ui.fontScaleStepMin;
			out << YAML::Key << "max" << YAML::Value << config.ui.fontScaleStepMax;
			out << YAML::Key << "slider_max" << YAML::Value << config.ui.fontScaleStepSliderMax;
			out << YAML::EndMap;
			out << YAML::Key << "settings_preview_enabled" << YAML::Value << config.ui.settingsPreviewEnabled;
			out << YAML::Key << "settings_auto_save_on_preview" << YAML::Value << config.ui.settingsAutoSaveOnPreview;
			out << YAML::EndMap;

			emitAppearance(out, config.appearance);

			out << YAML::Key << JobsSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "default_worker_threads" << YAML::Value << config.jobs.defaultWorkerThreadCount;
			out << YAML::Key << "reserve_urgent_worker" << YAML::Value << config.jobs.reserveUrgentWorker;
			out << YAML::EndMap;

			out << YAML::Key << EventQueueSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "initial_capacity" << YAML::Value << static_cast<unsigned long long>(config.eventQueue.initialCapacity);
			out << YAML::Key << "growth_step" << YAML::Value << static_cast<unsigned long long>(config.eventQueue.growthStep);
			out << YAML::EndMap;
			out << YAML::EndMap;
		}

		void emitUserSettings(YAML::Emitter &out, const ApplicationConfig &config)
		{
			out << YAML::BeginMap;
			out << YAML::Key << SchemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << UiSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "font" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "path" << YAML::Value << toPortablePathValue(config, config.ui.fontPath);
			out << YAML::EndMap;
			out << YAML::Key << "font_scale" << YAML::Value << config.ui.fontScale;
			out << YAML::Key << "font_scale_step" << YAML::Value << config.ui.fontScaleStep;
			out << YAML::Key << "settings_preview_enabled" << YAML::Value << config.ui.settingsPreviewEnabled;
			out << YAML::Key << "settings_auto_save_on_preview" << YAML::Value << config.ui.settingsAutoSaveOnPreview;
			out << YAML::EndMap;
			out << YAML::Key << WindowSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "x" << YAML::Value << config.window.x;
			out << YAML::Key << "y" << YAML::Value << config.window.y;
			out << YAML::Key << "width" << YAML::Value << config.window.width;
			out << YAML::Key << "height" << YAML::Value << config.window.height;
			out << YAML::Key << "maximized" << YAML::Value << config.window.maximized;
			out << YAML::EndMap;
			emitAppearance(out, config.appearance);
			out << YAML::EndMap;
		}

		void emitProfile(YAML::Emitter &out, const ApplicationConfig &config)
		{
			out << YAML::BeginMap;
			out << YAML::Key << SchemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << LogSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "level" << YAML::Value << ToString(config.log.level);
			out << YAML::Key << "to_file" << YAML::Value << config.log.toFile;
			out << YAML::Key << "file_path" << YAML::Value << config.log.filePath.String();
			out << YAML::EndMap;
			out << YAML::Key << TraceEventsKey << YAML::Value << config.log.traceEvents;

			out << YAML::Key << WindowSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "x" << YAML::Value << config.window.x;
			out << YAML::Key << "y" << YAML::Value << config.window.y;
			out << YAML::Key << "width" << YAML::Value << config.window.width;
			out << YAML::Key << "height" << YAML::Value << config.window.height;
			out << YAML::Key << "maximized" << YAML::Value << config.window.maximized;
			out << YAML::Key << "title" << YAML::Value << config.window.title;
			out << YAML::EndMap;

			out << YAML::Key << UiSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "font" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "path" << YAML::Value << toPortablePathValue(config, config.ui.fontPath);
			out << YAML::EndMap;
			out << YAML::Key << "font_scale" << YAML::Value << config.ui.fontScale;
			out << YAML::Key << "font_scale_step" << YAML::Value << config.ui.fontScaleStep;
			out << YAML::Key << "settings_preview_enabled" << YAML::Value << config.ui.settingsPreviewEnabled;
			out << YAML::Key << "settings_auto_save_on_preview" << YAML::Value << config.ui.settingsAutoSaveOnPreview;
			out << YAML::EndMap;

			emitAppearance(out, config.appearance);

			out << YAML::Key << JobsSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "default_worker_threads" << YAML::Value << config.jobs.defaultWorkerThreadCount;
			out << YAML::Key << "reserve_urgent_worker" << YAML::Value << config.jobs.reserveUrgentWorker;
			out << YAML::EndMap;

			out << YAML::Key << EventQueueSection << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "initial_capacity" << YAML::Value << static_cast<unsigned long long>(config.eventQueue.initialCapacity);
			out << YAML::Key << "growth_step" << YAML::Value << static_cast<unsigned long long>(config.eventQueue.growthStep);
			out << YAML::EndMap;
			out << YAML::EndMap;
		}
	} // namespace

	bool YamlConfigSerializer::SerializeDefaultConfig(
		const ApplicationConfig &config,
		std::string &text,
		std::string &error) const
	{
		YAML::Emitter out;
		emitDefaultConfig(out, config);
		return emitterToString(out, text, error);
	}

	bool YamlConfigSerializer::DeserializeDefaultConfig(
		std::string_view text,
		ApplicationConfig &config,
		std::string &error) const
	{
		YAML::Node root;
		if (!loadYamlText(text, root, error))
			return false;

		ConfigYaml::ApplyDefaultYaml(root, config);
		return true;
	}

	bool YamlConfigSerializer::SerializeUserSettings(
		const ApplicationConfig &config,
		std::string &text,
		std::string &error) const
	{
		YAML::Emitter out;
		emitUserSettings(out, config);
		return emitterToString(out, text, error);
	}

	bool YamlConfigSerializer::DeserializeUserSettings(
		std::string_view text,
		ApplicationConfig &config,
		std::string &error) const
	{
		YAML::Node root;
		if (!loadYamlText(text, root, error))
			return false;

		ConfigYaml::ApplyUserYaml(root, config);
		return true;
	}

	bool YamlConfigSerializer::SerializeConfigProfile(
		const ApplicationConfig &config,
		std::string &text,
		std::string &error) const
	{
		YAML::Emitter out;
		emitProfile(out, config);
		return emitterToString(out, text, error);
	}

	bool YamlConfigSerializer::DeserializeConfigProfile(
		std::string_view text,
		ApplicationConfig &config,
		std::string &error) const
	{
		YAML::Node root;
		if (!loadYamlText(text, root, error))
			return false;

		ConfigYaml::ApplyDefaultYaml(root, config);
		ConfigYaml::ApplyUserYaml(root, config);
		return true;
	}

	bool YamlConfigSerializer::SerializeAppearanceTheme(
		const AppearanceConfig &appearance,
		std::string &text,
		std::string &error) const
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << SchemaVersionKey << YAML::Value << ConfigManager::SchemaVersion;
		emitAppearance(out, appearance);
		out << YAML::EndMap;
		return emitterToString(out, text, error);
	}

	bool YamlConfigSerializer::DeserializeAppearanceTheme(
		std::string_view text,
		AppearanceConfig &appearance,
		std::string &error) const
	{
		YAML::Node root;
		if (!loadYamlText(text, root, error))
			return false;

		ConfigYaml::ApplyAppearanceYaml(root, appearance);
		return true;
	}
} // namespace DefectStudio

