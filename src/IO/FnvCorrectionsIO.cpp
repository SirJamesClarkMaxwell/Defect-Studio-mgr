#include "Core/dspch.hpp"

#include "IO/FnvCorrectionsIO.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
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

		template <typename Values>
		void EmitSequence(YAML::Emitter &emit, const Values &values)
		{
			emit << YAML::Flow << YAML::BeginSeq;
			for (const auto value : values)
				emit << value;
			emit << YAML::EndSeq;
		}

		template <typename Inputs>
		void EmitInputs(YAML::Emitter &emit, const Inputs &inputs)
		{
			emit << YAML::Key << "charge" << YAML::Value << inputs.charge
				<< YAML::Key << "dielectric" << YAML::Value << inputs.dielectric
				<< YAML::Key << "centreFractional" << YAML::Value;
			EmitSequence(emit, std::array{inputs.centreFractional.x, inputs.centreFractional.y, inputs.centreFractional.z});
			emit << YAML::Key << "energyCutoff" << YAML::Value << inputs.energyCutoff;
		}

		template <typename Inputs>
		void ReadInputs(const YAML::Node &node, Inputs &inputs)
		{
			inputs.charge = node["charge"].as<int>();
			inputs.dielectric = node["dielectric"].as<double>();
			inputs.energyCutoff = node["energyCutoff"].as<double>();
			const auto centre = node["centreFractional"];
			if (!centre.IsSequence() || centre.size() != 3)
				throw std::invalid_argument("centreFractional must have three coordinates");
			for (int a = 0; a < 3; ++a)
				inputs.centreFractional[a] = centre[a].as<double>();
		}

		PersistedFnvCorrection ReadEntry(const YAML::Node &node, const Path &projectDirectory)
		{
			if (!node.IsMap())
				throw std::invalid_argument("correction is not a map");
			PersistedFnvCorrection entry;
			entry.defectLocpot = ReadPath(node["defectLocpot"], projectDirectory);
			entry.bulkLocpot = ReadPath(node["bulkLocpot"], projectDirectory);
			if (entry.defectLocpot.Empty())
				throw std::invalid_argument("defect LOCPOT is empty");
			ReadInputs(node, entry);
			entry.dielectricSource = node["dielectricSource"] ? node["dielectricSource"].as<std::string>() : "";
			const auto convention = node["convention"].as<std::string>();
			if (convention != "Sxdefectalign" && convention != "Pymatgen")
				throw std::invalid_argument("unknown FNV convention");
			entry.convention = convention == "Pymatgen" ? FnvModelConvention::Pymatgen : FnvModelConvention::Sxdefectalign;
			const auto useAxis = node["useAxis"];
			if (!useAxis.IsSequence() || useAxis.size() != 3)
				throw std::invalid_argument("useAxis must have three values");
			for (int a = 0; a < 3; ++a)
				entry.useAxis[a] = useAxis[a].as<bool>();
			const auto profilesNode = node["profiles"];
			if (!profilesNode)
				return entry;
			FnvProfiles profiles;
			ReadInputs(profilesNode, profiles);
			profiles.latticeEnergy = profilesNode["latticeEnergy"].as<double>();
			profiles.modelPotentialMean = profilesNode["modelPotentialMean"].as<double>();
			const auto axes = profilesNode["axes"];
			if (!axes.IsSequence() || axes.size() != 3)
				throw std::invalid_argument("profiles must have three axes");
			for (int a = 0; a < 3; ++a)
			{
				auto &axis = profiles.axes[a];
				axis.length = axes[a]["length"].as<double>();
				axis.deltaV = axes[a]["deltaV"].as<std::vector<double>>();
				axis.modelPotential = axes[a]["modelPotential"].as<std::vector<double>>();
				const auto window = axes[a]["window"];
				if (!window.IsSequence() || window.size() != 2)
					throw std::invalid_argument("window must have a centre and width");
				entry.windows[a] = {window[0].as<double>(), window[1].as<double>()};
				if (!std::isfinite(axis.length) || axis.length <= 0.0 || axis.deltaV.size() < 2 ||
					axis.deltaV.size() != axis.modelPotential.size())
					throw std::invalid_argument("invalid axis length or mismatched profile arrays");
				for (std::size_t k = 0; k < axis.deltaV.size(); ++k)
					axis.position.push_back(static_cast<double>(k) * axis.length / static_cast<double>(axis.deltaV.size()));
				// Domain validation also checks finite values and the saved window.
				(void)AlignFnvInWindow(axis, FnvModelPotential(profiles, a, entry.convention), entry.windows[a]);
			}
			if (!std::isfinite(profiles.latticeEnergy) || !std::isfinite(profiles.modelPotentialMean))
				throw std::invalid_argument("nonfinite profile energy or mean potential");
			entry.profiles = std::move(profiles);
			return entry;
		}
	} // namespace

	int UpsertFnvCorrection(FnvCorrectionsFile &file, PersistedFnvCorrection entry)
	{
		if (entry.defectLocpot.Empty())
			return -1;
		const auto found = std::find_if(file.corrections.begin(), file.corrections.end(), [&](const auto &saved) {
			return saved.defectLocpot.Native().lexically_normal() == entry.defectLocpot.Native().lexically_normal();
		});
		file.active = static_cast<int>(found - file.corrections.begin());
		if (found == file.corrections.end())
			file.corrections.push_back(std::move(entry));
		else
			*found = std::move(entry);
		return file.active;
	}

	Path FnvCorrectionsIO::FilePath(const Path &projectDirectory)
	{
		return projectDirectory / "fnv_corrections.yaml";
	}

	bool FnvCorrectionsIO::Parse(const std::string &text, const Path &projectDirectory,
		FnvCorrectionsFile &outFile, std::vector<std::string> &outWarnings, std::string &outError)
	{
		outFile = {};
		outWarnings.clear();
		outError.clear();
		try
		{
			if (text.find_first_not_of(" \t\r\n") == std::string::npos)
				return true;
			const auto root = YAML::Load(text);
			if (!root.IsMap())
				throw std::invalid_argument("fnv_corrections.yaml root is not a map");
			FnvCorrectionsFile file;
			file.formatVersion = root["formatVersion"] ? root["formatVersion"].as<int>() : kFormatVersion;
			if (file.formatVersion > kFormatVersion)
				throw std::invalid_argument("fnv_corrections.yaml uses a future format version");
			const int active = root["active"] ? root["active"].as<int>() : -1;
			const auto entries = root["corrections"];
			if (entries && !entries.IsSequence())
				throw std::invalid_argument("corrections is not a sequence");
			for (std::size_t i = 0; entries && i < entries.size(); ++i)
			{
				try
				{
					auto entry = ReadEntry(entries[i], projectDirectory);
					if (active >= 0 && i == static_cast<std::size_t>(active))
						file.active = static_cast<int>(file.corrections.size());
					file.corrections.push_back(std::move(entry));
				}
				catch (const std::exception &exception)
				{
					std::string warning = "Dropped FNV correction " + std::to_string(i) + ": " + exception.what();
					std::replace_if(warning.begin(), warning.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
					outWarnings.push_back(std::move(warning));
				}
			}
			outFile = std::move(file);
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	std::string FnvCorrectionsIO::Serialize(const FnvCorrectionsFile &file, const Path &projectDirectory)
	{
		YAML::Emitter emit;
		emit << YAML::Precision(17) << YAML::BeginMap
			<< YAML::Key << "formatVersion" << YAML::Value << kFormatVersion
			<< YAML::Key << "active" << YAML::Value << file.active
			<< YAML::Key << "corrections" << YAML::Value << YAML::BeginSeq;
		for (const auto &entry : file.corrections)
		{
			emit << YAML::BeginMap
				<< YAML::Key << "defectLocpot" << YAML::Value << StoredPath(entry.defectLocpot, projectDirectory)
				<< YAML::Key << "bulkLocpot" << YAML::Value << StoredPath(entry.bulkLocpot, projectDirectory);
			EmitInputs(emit, entry);
			emit << YAML::Key << "dielectricSource" << YAML::Value << entry.dielectricSource
				<< YAML::Key << "convention" << YAML::Value
				<< (entry.convention == FnvModelConvention::Sxdefectalign ? "Sxdefectalign" : "Pymatgen")
				<< YAML::Key << "useAxis" << YAML::Value;
			EmitSequence(emit, entry.useAxis);
			if (entry.profiles)
			{
				const auto &profiles = *entry.profiles;
				emit << YAML::Key << "profiles" << YAML::Value << YAML::BeginMap;
				EmitInputs(emit, profiles);
				emit << YAML::Key << "latticeEnergy" << YAML::Value << profiles.latticeEnergy
					<< YAML::Key << "modelPotentialMean" << YAML::Value << profiles.modelPotentialMean
					<< YAML::Key << "axes" << YAML::Value << YAML::BeginSeq;
				for (int a = 0; a < 3; ++a)
				{
					const auto &axis = profiles.axes[a];
					emit << YAML::BeginMap << YAML::Key << "length" << YAML::Value << axis.length
						<< YAML::Key << "deltaV" << YAML::Value;
					EmitSequence(emit, axis.deltaV);
					emit << YAML::Key << "modelPotential" << YAML::Value;
					EmitSequence(emit, axis.modelPotential);
					emit << YAML::Key << "window" << YAML::Value;
					EmitSequence(emit, std::array{entry.windows[a].centre, entry.windows[a].width});
					emit << YAML::EndMap;
				}
				emit << YAML::EndSeq << YAML::EndMap;
			}
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::EndMap;
		if (!emit.good())
			throw std::runtime_error(emit.GetLastError());
		return emit.c_str();
	}

	bool FnvCorrectionsIO::Load(const Path &projectDirectory, FnvCorrectionsFile &outFile,
		std::vector<std::string> &outWarnings, std::string &outError)
	{
		outFile = {};
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
			return Parse(text, projectDirectory, outFile, outWarnings, outError);
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	bool FnvCorrectionsIO::Save(const Path &projectDirectory, const FnvCorrectionsFile &file, std::string &outError)
	{
		outError.clear();
		const Path target = FilePath(projectDirectory);
		auto tempNative = target.Native();
		tempNative += ".tmp";
		const Path temp = Path::FromResolved(std::move(tempNative));
		try
		{
			const auto text = Serialize(file, projectDirectory);
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
