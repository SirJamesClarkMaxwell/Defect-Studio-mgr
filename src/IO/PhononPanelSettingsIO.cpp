#include "Core/dspch.hpp"

#include "IO/PhononPanelSettingsIO.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <regex>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string StoredPath(const Path &path, const Path &projectDirectory)
		{
			if (path.Empty())
				return {};
			auto native = path.Native().lexically_normal();
			const auto relative = native.lexically_relative(projectDirectory.Native().lexically_normal());
			if (!projectDirectory.Empty() && !relative.empty() && !relative.is_absolute() &&
				std::none_of(relative.begin(), relative.end(), [](const auto &part) { return part == ".."; }))
				native = relative;
			const auto utf8 = native.generic_u8string();
			return std::string(utf8.begin(), utf8.end());
		}

		Path ReadPath(const YAML::Node &node, const Path &projectDirectory)
		{
			const Path path = Path::FromUtf8(node.as<std::string>());
			if (path.Empty())
				return {};
			return Path::FromResolved((path.Native().is_absolute() ? path.Native() :
				projectDirectory.Native() / path.Native()).lexically_normal());
		}

		void Warn(std::vector<std::string> &warnings, const std::string &key, const std::string &reason)
		{
			std::string warning = "Ignored phonon setting " + key + ": " + reason;
			std::replace_if(warning.begin(), warning.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
			warnings.push_back(std::move(warning));
		}

		template <typename T, typename Read>
		void ReadSetting(const YAML::Node &map, const std::string &key, T &value,
			std::vector<std::string> &warnings, Read read)
		{
			if (!map || !map[key])
				return;
			try
			{
				if (map[key].IsNull())
					throw std::invalid_argument("expected a value, not null");
				value = read(map[key]);
			}
			catch (const std::exception &exception)
			{
				Warn(warnings, key, exception.what());
			}
		}

		void ReadNumber(const YAML::Node &map, const std::string &key, double &value,
			std::vector<std::string> &warnings, double minimum = 0.0,
			double maximum = std::numeric_limits<double>::max(), bool exclusiveMinimum = false)
		{
			ReadSetting(map, key, value, warnings, [=](const YAML::Node &node) {
				const double number = node.as<double>();
				if (!std::isfinite(number) || number < minimum || number > maximum ||
					(exclusiveMinimum && number == minimum))
					throw std::invalid_argument("value is outside its valid range");
				return number;
			});
		}

		glm::vec3 ReadColor(const YAML::Node &node)
		{
			if (!node.IsSequence() || node.size() != 3)
				throw std::invalid_argument("color must contain three components");
			glm::vec3 color;
			for (int i = 0; i < 3; ++i)
			{
				color[i] = node[i].as<float>();
				if (!std::isfinite(color[i]) || color[i] < 0.0f || color[i] > 1.0f)
					throw std::invalid_argument("color components must be finite and between 0 and 1");
			}
			return color;
		}

		void EmitColor(YAML::Emitter &emit, const glm::vec3 &color)
		{
			emit << YAML::Flow << YAML::BeginSeq << color.x << color.y << color.z << YAML::EndSeq;
		}

		void ReadSeriesStyles(const YAML::Node &styles, PhononPanelSettings &settings, std::vector<std::string> &warnings)
		{
			if (!styles) return;
			if (!styles.IsMap()) { Warn(warnings, "seriesStyles", "expected a map"); return; }
			for (const auto &entry : styles)
			{
				try
				{
					const auto key = entry.first.as<std::string>();
					const auto map = entry.second;
					if (!map.IsMap()) { Warn(warnings, "seriesStyles/" + key, "expected a map"); continue; }
					PlotSeriesStyle style;
					if (map["color"] && !map["color"].IsNull())
						ReadSetting(map, "color", style.color, warnings, [](const YAML::Node &node) {
							if (!node.IsSequence() || node.size() != 4) throw std::invalid_argument("color must contain four components");
							glm::vec4 color;
							for (int i = 0; i < 4; ++i)
							{
								color[i] = node[i].as<float>();
								if (!std::isfinite(color[i]) || color[i] < 0.0f || color[i] > 1.0f)
									throw std::invalid_argument("color components must be finite and between 0 and 1");
							}
							return color;
						});
					double weight = style.lineWeight, alpha = style.fillAlpha;
					ReadNumber(map, "weight", weight, warnings, 0.5, 6.0);
					ReadNumber(map, "fillAlpha", alpha, warnings, 0.0, 1.0);
					style.lineWeight = static_cast<float>(weight);
					style.fillAlpha = static_cast<float>(alpha);
					const auto boolean = [](const YAML::Node &node) { return node.as<bool>(); };
					ReadSetting(map, "visible", style.visible, warnings, boolean);
					ReadSetting(map, "filled", style.filled, warnings, boolean);
					ReadSetting(map, "lineStyle", style.lineStyle, warnings, [](const YAML::Node &node) {
						const int value = node.as<int>();
						if (value < 0 || value > 2) throw std::invalid_argument("unknown line style");
						return value;
					});
					settings.seriesStyles.emplace(key, std::move(style));
				}
				catch (const std::exception &exception) { Warn(warnings, "seriesStyles", exception.what()); }
			}
		}
	} // namespace

	Path PhononPanelSettingsIO::FilePath(const Path &projectDirectory)
	{
		return projectDirectory / "phonon_panel.yaml";
	}

	bool PhononPanelSettingsIO::Parse(const std::string &text, const Path &projectDirectory,
		PhononPanelSettings &outSettings, std::vector<std::string> &outWarnings, std::string &outError)
	{
		outSettings = {};
		outWarnings.clear();
		outError.clear();
		try
		{
			if (text.find_first_not_of(" \t\r\n") == std::string::npos)
				return true;
			const auto root = YAML::Load(text);
			if (!root.IsMap())
				throw std::invalid_argument("phonon_panel.yaml root is not a map");
			int version = kFormatVersion;
			ReadSetting(root, "formatVersion", version, outWarnings, [](const YAML::Node &node) {
				const int number = node.as<int>();
				if (number < 1)
					throw std::invalid_argument("formatVersion must be positive");
				return number;
			});
			if (version > kFormatVersion)
				throw std::invalid_argument("phonon_panel.yaml uses a future format version");

			PhononPanelSettings settings;
			ReadSetting(root, "arrowsFollowPhase", settings.arrowsFollowPhase, outWarnings,
				[](const YAML::Node &node) { return node.as<bool>(); });
			ReadSeriesStyles(root["seriesStyles"], settings, outWarnings);
			const auto readPath = [&](const YAML::Node &node) { return ReadPath(node, projectDirectory); };
			const auto inputs = root["inputs"];
			if (inputs && !inputs.IsMap())
				Warn(outWarnings, "inputs", "expected a map");
			else
			{
				ReadSetting(inputs, "fcDir", settings.forceConstantDirectory, outWarnings, readPath);
				ReadSetting(inputs, "gsDir", settings.groundStateDirectory, outWarnings, readPath);
				ReadSetting(inputs, "esDir", settings.excitedStateDirectory, outWarnings, readPath);
				ReadSetting(inputs, "tripletDir", settings.tripletDirectory, outWarnings, readPath);
				ReadSetting(inputs, "zplExp", settings.experimentalZplEv, outWarnings, [](const YAML::Node &node) {
					const double number = node.as<double>();
					if (!std::isfinite(number) || number <= 0.0)
						throw std::invalid_argument("experimental ZPL must be positive and finite");
					return number;
				});
				ReadNumber(inputs, "sigma", settings.sigmaMeV, outWarnings, 0.0, std::numeric_limits<double>::max(), true);
				ReadNumber(inputs, "gamma", settings.gammaMeV, outWarnings, 0.0, std::numeric_limits<double>::max(), true);
				ReadSetting(inputs, "bulkFcDir", settings.bulkForceConstantDirectory, outWarnings, readPath);
				ReadSetting(inputs, "mult", settings.embeddingMult, outWarnings, [](const YAML::Node &node) {
					const auto value = node.as<std::string>();
					std::smatch parts;
					if (!value.empty())
					{
						if (!std::regex_match(value, parts, std::regex("([1-9][0-9]*)x([1-9][0-9]*)x([1-9][0-9]*)")))
							throw std::invalid_argument("mult must contain three positive integers, e.g. 12x12x1");
						for (int i = 1; i <= 3; ++i) (void)std::stoi(parts[i].str());
					}
					return value;
				});
				const auto readCutoff = [](const YAML::Node &node) {
					const double number = node.as<double>();
					if (!std::isfinite(number) || number <= 0.0)
						throw std::invalid_argument("cutoff must be positive and finite");
					return number;
				};
				ReadSetting(inputs, "dcutoff", settings.dcutoffAngstrom, outWarnings, readCutoff);
				ReadSetting(inputs, "bcutoff", settings.bcutoffAngstrom, outWarnings, readCutoff);
				ReadSetting(inputs, "ccdDir", settings.ccdOutputDirectory, outWarnings, readPath);
				ReadNumber(inputs, "ccdStep", settings.ccdStep, outWarnings, 0.0, std::numeric_limits<double>::max(), true);
				ReadSetting(inputs, "dispersionDir", settings.dispersionDirectory, outWarnings, readPath);
				ReadSetting(inputs, "dispersionInPlane", settings.dispersionInPlane, outWarnings,
					[](const YAML::Node &node) { return node.as<bool>(); });
				ReadSetting(inputs, "dispersionPoints", settings.dispersionPoints, outWarnings, [](const YAML::Node &node) {
					const int points = node.as<int>();
					if (points < 2) throw std::invalid_argument("dispersionPoints must be at least 2");
					return points;
				});
			}
			ReadSetting(root, "resultDir", settings.resultDirectory, outWarnings, readPath);
			const auto view = root["view"];
			if (view && !view.IsMap())
				Warn(outWarnings, "view", "expected a map");
			else
			{
				ReadSetting(view, "selectedMode", settings.selectedMode, outWarnings, [](const YAML::Node &node) {
					const int mode = node.as<int>();
					if (mode < -1)
						throw std::invalid_argument("selectedMode must be -1 or a nonnegative index");
					return mode;
				});
				ReadNumber(view, "participationThreshold", settings.participationThreshold, outWarnings, 0.0, 1.0);
				ReadNumber(view, "carriedFraction", settings.carriedFraction, outWarnings, 0.0, 1.0, true);
				ReadNumber(view, "arrowLength", settings.arrowLengthAngstrom, outWarnings, 0.0,
					std::numeric_limits<double>::max(), true);
				ReadSetting(view, "showArrows", settings.showArrows, outWarnings,
					[](const YAML::Node &node) { return node.as<bool>(); });
				ReadSetting(view, "arrowLowColor", settings.arrowLowColor, outWarnings, ReadColor);
				ReadSetting(view, "arrowHighColor", settings.arrowHighColor, outWarnings, ReadColor);
				ReadNumber(view, "amplitude", settings.animationAmplitudeAngstrom, outWarnings);
				ReadNumber(view, "period", settings.animationPeriodSeconds, outWarnings, 0.0,
					std::numeric_limits<double>::max(), true);
				ReadSetting(view, "plZpl", settings.plZpl, outWarnings, [](const YAML::Node &node) {
					const auto zpl = node.as<std::string>();
					if (zpl != "computed" && zpl != "spin_purified" && zpl != "experimental")
						throw std::invalid_argument("unknown PL ZPL choice");
					return zpl;
				});
				ReadSetting(view, "irrepFilter", settings.irrepFilter, outWarnings,
					[](const YAML::Node &node) { return node.as<std::string>(); });
			}
			outSettings = std::move(settings);
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	std::string PhononPanelSettingsIO::Serialize(const PhononPanelSettings &settings, const Path &projectDirectory)
	{
		YAML::Emitter emit;
		emit << YAML::Precision(17) << YAML::BeginMap
			<< YAML::Key << "formatVersion" << YAML::Value << kFormatVersion
			<< YAML::Key << "arrowsFollowPhase" << YAML::Value << settings.arrowsFollowPhase
			<< YAML::Key << "seriesStyles" << YAML::Value << YAML::BeginMap;
		for (const auto &[key, style] : settings.seriesStyles)
		{
			emit << YAML::Key << key << YAML::Value << YAML::BeginMap << YAML::Key << "color" << YAML::Value;
			if (style.color)
				emit << YAML::Flow << YAML::BeginSeq << style.color->x << style.color->y << style.color->z << style.color->w << YAML::EndSeq;
			else emit << YAML::Null;
			emit << YAML::Key << "weight" << YAML::Value << style.lineWeight
				<< YAML::Key << "visible" << YAML::Value << style.visible
				<< YAML::Key << "filled" << YAML::Value << style.filled
				<< YAML::Key << "fillAlpha" << YAML::Value << style.fillAlpha
				<< YAML::Key << "lineStyle" << YAML::Value << style.lineStyle << YAML::EndMap;
		}
		emit << YAML::EndMap
			<< YAML::Key << "inputs" << YAML::Value << YAML::BeginMap
			<< YAML::Key << "fcDir" << YAML::Value << StoredPath(settings.forceConstantDirectory, projectDirectory)
			<< YAML::Key << "gsDir" << YAML::Value << StoredPath(settings.groundStateDirectory, projectDirectory)
			<< YAML::Key << "esDir" << YAML::Value << StoredPath(settings.excitedStateDirectory, projectDirectory)
			<< YAML::Key << "tripletDir" << YAML::Value << StoredPath(settings.tripletDirectory, projectDirectory);
		if (settings.experimentalZplEv)
			emit << YAML::Key << "zplExp" << YAML::Value << *settings.experimentalZplEv;
		emit << YAML::Key << "sigma" << YAML::Value << settings.sigmaMeV
			<< YAML::Key << "gamma" << YAML::Value << settings.gammaMeV
			<< YAML::Key << "bulkFcDir" << YAML::Value << StoredPath(settings.bulkForceConstantDirectory, projectDirectory)
			<< YAML::Key << "mult" << YAML::Value << settings.embeddingMult;
		if (settings.dcutoffAngstrom)
			emit << YAML::Key << "dcutoff" << YAML::Value << *settings.dcutoffAngstrom;
		if (settings.bcutoffAngstrom)
			emit << YAML::Key << "bcutoff" << YAML::Value << *settings.bcutoffAngstrom;
		emit << YAML::Key << "ccdDir" << YAML::Value << StoredPath(settings.ccdOutputDirectory, projectDirectory)
			<< YAML::Key << "ccdStep" << YAML::Value << settings.ccdStep
			<< YAML::Key << "dispersionDir" << YAML::Value << StoredPath(settings.dispersionDirectory, projectDirectory)
			<< YAML::Key << "dispersionInPlane" << YAML::Value << settings.dispersionInPlane
			<< YAML::Key << "dispersionPoints" << YAML::Value << settings.dispersionPoints << YAML::EndMap
			<< YAML::Key << "resultDir" << YAML::Value << StoredPath(settings.resultDirectory, projectDirectory)
			<< YAML::Key << "view" << YAML::Value << YAML::BeginMap
			<< YAML::Key << "selectedMode" << YAML::Value << settings.selectedMode
			<< YAML::Key << "participationThreshold" << YAML::Value << settings.participationThreshold
			<< YAML::Key << "carriedFraction" << YAML::Value << settings.carriedFraction
			<< YAML::Key << "arrowLength" << YAML::Value << settings.arrowLengthAngstrom
			<< YAML::Key << "showArrows" << YAML::Value << settings.showArrows
			<< YAML::Key << "arrowLowColor" << YAML::Value;
		EmitColor(emit, settings.arrowLowColor);
		emit << YAML::Key << "arrowHighColor" << YAML::Value;
		EmitColor(emit, settings.arrowHighColor);
		emit << YAML::Key << "amplitude" << YAML::Value << settings.animationAmplitudeAngstrom
			<< YAML::Key << "period" << YAML::Value << settings.animationPeriodSeconds
			<< YAML::Key << "plZpl" << YAML::Value << settings.plZpl
			<< YAML::Key << "irrepFilter" << YAML::Value << settings.irrepFilter << YAML::EndMap << YAML::EndMap;
		if (!emit.good())
			throw std::runtime_error(emit.GetLastError());
		return emit.c_str();
	}

	bool PhononPanelSettingsIO::Load(const Path &projectDirectory, PhononPanelSettings &outSettings,
		std::vector<std::string> &outWarnings, std::string &outError)
	{
		outSettings = {};
		outWarnings.clear();
		outError.clear();
		try
		{
			const Path path = FilePath(projectDirectory);
			if (!FileSystem::Exists(path.Native()))
				return true;
			std::string text;
			if (!TextFileIO::Load(path, text, outError))
				return false;
			return Parse(text, projectDirectory, outSettings, outWarnings, outError);
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	bool PhononPanelSettingsIO::Save(const Path &projectDirectory, const PhononPanelSettings &settings, std::string &outError)
	{
		outError.clear();
		const Path target = FilePath(projectDirectory);
		auto tempNative = target.Native();
		tempNative += ".tmp";
		const Path temp = Path::FromResolved(std::move(tempNative));
		try
		{
			const auto text = Serialize(settings, projectDirectory);
			std::error_code error;
			FileSystem::CreateDirectories(target.parent_path().Native(), error);
			if (error)
				throw std::runtime_error(error.message());
			// Check close as well as write before replacing the previous file.
			std::ofstream stream(temp.Native(), std::ios::binary | std::ios::trunc);
			stream.write(text.data(), static_cast<std::streamsize>(text.size()));
			stream.close();
			if (!stream)
				throw std::runtime_error("Unable to write " + temp.Utf8());
			if (!FileSystem::Rename(temp.Native(), target.Native(), error))
				throw std::runtime_error(error.message());
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			std::error_code ignored;
			FileSystem::Remove(temp.Native(), ignored);
			return false;
		}
	}
} // namespace DefectStudio
