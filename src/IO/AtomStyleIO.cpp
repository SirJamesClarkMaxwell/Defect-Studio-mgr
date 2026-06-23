#include "Core/dspch.hpp"

#include "IO/AtomStyleIO.hpp"

#include <algorithm>
#include <cctype>

#include <yaml-cpp/yaml.h>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool ParseColorNode(const YAML::Node &colorNode, glm::vec3 &outColor)
		{
			if (!colorNode || !colorNode.IsSequence() || colorNode.size() < 3)
				return false;

			outColor.x = colorNode[0].as<float>(outColor.x);
			outColor.y = colorNode[1].as<float>(outColor.y);
			outColor.z = colorNode[2].as<float>(outColor.z);
			return true;
		}

		[[nodiscard]] std::string ToLowerAscii(std::string_view value)
		{
			std::string lowered;
			lowered.reserve(value.size());
			for (const char character : value)
			{
				const unsigned char raw = static_cast<unsigned char>(character);
				lowered.push_back(static_cast<char>(std::tolower(raw)));
			}
			return lowered;
		}

		[[nodiscard]] VacancyRenderMode ParseVacancyRenderMode(std::string_view value)
		{
			const std::string normalized = ToLowerAscii(value);
			if (normalized == "wireframe")
				return VacancyRenderMode::Wireframe;
			if (normalized == "solid")
				return VacancyRenderMode::Solid;
			return VacancyRenderMode::Ghost;
		}
	} // namespace

	bool AtomStyleIO::LoadFromFile(
		const Path &path,
		std::unordered_map<std::string, AtomRenderStyle> &outStyles,
		VacancyRenderStyle &outVacancyStyle,
		std::string &outError)
	{
		std::string text;
		if (!TextFileIO::Load(path, text, outError))
			return false;

		return ParseYaml(text, outStyles, outVacancyStyle, outError);
	}

	bool AtomStyleIO::ParseYaml(
		std::string_view yamlText,
		std::unordered_map<std::string, AtomRenderStyle> &outStyles,
		VacancyRenderStyle &outVacancyStyle,
		std::string &outError)
	{
		try
		{
			YAML::Node root = YAML::Load(std::string(yamlText));
			if (!root || !root.IsMap())
			{
				outError = "Atom style YAML root is not a map";
				return false;
			}

			const YAML::Node vacancyNode = root["vacancy"];
			if (vacancyNode && vacancyNode.IsMap())
			{
				const bool parsedVacancyColor = ParseColorNode(vacancyNode["color"], outVacancyStyle.color);
				(void)parsedVacancyColor;
				outVacancyStyle.displayRadius = vacancyNode["display_radius"].as<float>(outVacancyStyle.displayRadius);
				outVacancyStyle.opacity = vacancyNode["opacity"].as<float>(outVacancyStyle.opacity);
				outVacancyStyle.renderMode = ParseVacancyRenderMode(vacancyNode["render_mode"].as<std::string>("ghost"));
			}

			const YAML::Node elementsNode = root["elements"];
			if (!elementsNode || !elementsNode.IsMap())
			{
				outError = "Atom style YAML has no 'elements' map";
				return false;
			}

			outStyles.clear();
			for (const auto &entry : elementsNode)
			{
				if (!entry.first.IsScalar() || !entry.second.IsMap())
					continue;

				const std::string symbol = entry.first.as<std::string>("");
				if (symbol.empty())
					continue;

				AtomRenderStyle style;
				const YAML::Node styleNode = entry.second;
				const bool parsedStyleColor = ParseColorNode(styleNode["color"], style.color);
				(void)parsedStyleColor;
				style.displayRadius = styleNode["display_radius"].as<float>(style.displayRadius);
				outStyles[symbol] = style;
			}

			if (outStyles.empty())
			{
				outError = "Atom style YAML does not contain valid entries";
				return false;
			}

			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}
} // namespace DefectStudio
