#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "App/ApplicationState.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class IYamlCodec;

	enum class YamlCodecKind
	{
		ApplicationConfig,
	};

	class YamlCodecFacade final
	{
	public:
		YamlCodecFacade();

		[[nodiscard]] static YamlCodecFacade &Default();
		void RegisterCodec(YamlCodecKind kind, Unique<IYamlCodec> codec);

		[[nodiscard]] bool SerializeDefaultConfig(
			const ApplicationConfig &config,
			std::string &text,
			std::string &error) const;
		[[nodiscard]] bool DeserializeDefaultConfig(
			std::string_view text,
			ApplicationConfig &config,
			std::string &error) const;

		[[nodiscard]] bool SerializeUserSettings(
			const ApplicationConfig &config,
			std::string &text,
			std::string &error) const;
		[[nodiscard]] bool DeserializeUserSettings(
			std::string_view text,
			ApplicationConfig &config,
			std::string &error) const;

		[[nodiscard]] bool SerializeConfigProfile(
			const ApplicationConfig &config,
			std::string &text,
			std::string &error) const;
		[[nodiscard]] bool DeserializeConfigProfile(
			std::string_view text,
			ApplicationConfig &config,
			std::string &error) const;

		[[nodiscard]] bool SerializeAppearanceTheme(
			const AppearanceConfig &appearance,
			std::string &text,
			std::string &error) const;
		[[nodiscard]] bool DeserializeAppearanceTheme(
			std::string_view text,
			AppearanceConfig &appearance,
			std::string &error) const;

	private:
		[[nodiscard]] const IYamlCodec *codecFor(YamlCodecKind kind) const;

	private:
		std::unordered_map<YamlCodecKind, Unique<IYamlCodec>> m_Codecs;
	};
} // namespace DefectStudio