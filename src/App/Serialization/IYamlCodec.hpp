#pragma once

#include <string>
#include <string_view>

#include "App/ApplicationState.hpp"

namespace DefectStudio
{
    class IYamlCodec
    {
    public:
        virtual ~IYamlCodec() = default;

        [[nodiscard]] virtual bool SerializeDefaultConfig(
            const ApplicationConfig &config,
            std::string &text,
            std::string &error) const = 0;
        [[nodiscard]] virtual bool DeserializeDefaultConfig(
            std::string_view text,
            ApplicationConfig &config,
            std::string &error) const = 0;

        [[nodiscard]] virtual bool SerializeUserSettings(
            const ApplicationConfig &config,
            std::string &text,
            std::string &error) const = 0;
        [[nodiscard]] virtual bool DeserializeUserSettings(
            std::string_view text,
            ApplicationConfig &config,
            std::string &error) const = 0;

        [[nodiscard]] virtual bool SerializeConfigProfile(
            const ApplicationConfig &config,
            std::string &text,
            std::string &error) const = 0;
        [[nodiscard]] virtual bool DeserializeConfigProfile(
            std::string_view text,
            ApplicationConfig &config,
            std::string &error) const = 0;

        [[nodiscard]] virtual bool SerializeAppearanceTheme(
            const AppearanceConfig &appearance,
            std::string &text,
            std::string &error) const = 0;
        [[nodiscard]] virtual bool DeserializeAppearanceTheme(
            std::string_view text,
            AppearanceConfig &appearance,
            std::string &error) const = 0;
    };
} // namespace DefectStudio
