#include "Core/dspch.hpp"

#include "App/Serialization/YamlCodecFacade.hpp"

#include <utility>

#include "App/Serialization/YamlConfigSerializer.hpp"

namespace
{
	constexpr const char *kNoCodecError = "No YAML codec registered for requested document kind";
}

namespace DefectStudio
{
	YamlCodecFacade::YamlCodecFacade()
	{
		m_Codecs.emplace(YamlCodecKind::ApplicationConfig, CreateUnique<YamlConfigSerializer>());
	}

	YamlCodecFacade &YamlCodecFacade::Default()
	{
		static YamlCodecFacade instance;
		return instance;
	}

	void YamlCodecFacade::RegisterCodec(YamlCodecKind kind, Unique<IYamlCodec> codec)
	{
		if (!codec)
			return;

		m_Codecs[kind] = std::move(codec);
	}

	const IYamlCodec *YamlCodecFacade::codecFor(YamlCodecKind kind) const
	{
		auto it = m_Codecs.find(kind);
		if (it == m_Codecs.end() || !it->second)
			return nullptr;

		return it->second.get();
	}

	bool YamlCodecFacade::SerializeDefaultConfig(const ApplicationConfig &config, std::string &text, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->SerializeDefaultConfig(config, text, error);
	}

	bool YamlCodecFacade::DeserializeDefaultConfig(std::string_view text, ApplicationConfig &config, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->DeserializeDefaultConfig(text, config, error);
	}

	bool YamlCodecFacade::SerializeUserSettings(const ApplicationConfig &config, std::string &text, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->SerializeUserSettings(config, text, error);
	}

	bool YamlCodecFacade::DeserializeUserSettings(std::string_view text, ApplicationConfig &config, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->DeserializeUserSettings(text, config, error);
	}

	bool YamlCodecFacade::SerializeConfigProfile(const ApplicationConfig &config, std::string &text, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->SerializeConfigProfile(config, text, error);
	}

	bool YamlCodecFacade::DeserializeConfigProfile(std::string_view text, ApplicationConfig &config, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->DeserializeConfigProfile(text, config, error);
	}

	bool YamlCodecFacade::SerializeAppearanceTheme(const AppearanceConfig &appearance, std::string &text, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->SerializeAppearanceTheme(appearance, text, error);
	}

	bool YamlCodecFacade::DeserializeAppearanceTheme(std::string_view text, AppearanceConfig &appearance, std::string &error) const
	{
		const IYamlCodec *codec = codecFor(YamlCodecKind::ApplicationConfig);
		if (!codec)
		{
			error = kNoCodecError;
			return false;
		}

		return codec->DeserializeAppearanceTheme(text, appearance, error);
	}
} // namespace DefectStudio
