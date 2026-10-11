#include "Core/dspch.hpp"
#include "IO/EnergeticsIO.hpp"

#include <cmath>
#include <limits>
#include <yaml-cpp/yaml.h>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string StoredPath(const Path &path, const Path &projectDirectory)
		{
			if (path.Empty()) return {};
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
			if (path.Empty()) return {};
			return Path::FromResolved((path.Native().is_absolute() ? path.Native() :
				projectDirectory.Native() / path.Native()).lexically_normal());
		}

		void RequireMap(const YAML::Node &node)
		{
			if (!node.IsMap()) throw std::invalid_argument("expected a map");
		}

		template <typename T>
		void ReadValue(const YAML::Node &node, const std::string &key, T &value)
		{
			if (node[key]) value = node[key].as<T>();
			if constexpr (std::is_floating_point_v<T>)
				if (!std::isfinite(value)) throw std::invalid_argument(key + " must be finite");
		}

		void ReadOptional(const YAML::Node &node, const std::string &key, std::optional<double> &value)
		{
			if (!node[key]) return;
			double number = 0.0;
			ReadValue(node, key, number);
			value = number;
		}

		void ReadOptionalPath(const YAML::Node &node, const std::string &key, const Path &project, std::optional<Path> &value)
		{
			if (!node[key]) return;
			value = ReadPath(node[key], project);
			if (value->Empty()) throw std::invalid_argument(key + " is empty");
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		template <typename T>
		std::map<std::string, T> ReadSpeciesMap(const YAML::Node &node)
		{
			RequireMap(node);
			std::map<std::string, T> values;
			for (const auto &entry : node)
			{
				const auto species = entry.first.as<std::string>();
				const auto value = entry.second.as<T>();
				if (species.empty()) throw std::invalid_argument("empty species");
				if constexpr (std::is_floating_point_v<T>)
					if (!std::isfinite(value)) throw std::invalid_argument("nonfinite chemical potential");
				if constexpr (std::is_integral_v<T>)
					if (value <= 0) throw std::invalid_argument("formula counts must be positive");
				values.emplace(species, value);
			}
			return values;
		}

		VaspRunRecord ReadRun(const YAML::Node &node, const Path &project)
		{
			RequireMap(node);
			VaspRunRecord run;
			run.directory = ReadPath(node["directory"], project);
			if (run.directory.Empty()) throw std::invalid_argument("empty run directory");
			ReadValue(node, "species", run.species);
			ReadValue(node, "ions", run.ions);
			ReadValue(node, "valence", run.valence);
			ReadOptional(node, "electrons", run.electrons);
			ReadOptional(node, "totalEnergy", run.totalEnergy);
			ReadValue(node, "finished", run.finished);
			ReadValue(node, "relaxation", run.relaxation);
			ReadValue(node, "ionicConverged", run.ionicConverged);
			ReadValue(node, "hasLocpot", run.hasLocpot);
			ReadValue(node, "hasContcar", run.hasContcar);
			ReadValue(node, "outcarSize", run.outcarSize);
			ReadValue(node, "outcarWriteTime", run.outcarWriteTime);
			ReadValue(node, "error", run.error);
			if (run.species.size() != run.ions.size() || (!run.valence.empty() && run.valence.size() != run.ions.size()))
				throw std::invalid_argument("mismatched species, ions or valence arrays");
			for (auto count : run.ions)
				if (count < 0) throw std::invalid_argument("negative ion count");
			for (auto valence : run.valence)
				if (!std::isfinite(valence)) throw std::invalid_argument("nonfinite valence");
			if (const auto method = node["method"])
			{
				RequireMap(method);
				ReadValue(method, "hybrid", run.method.hybrid);
				ReadValue(method, "hfscreen", run.method.hfscreen);
				ReadValue(method, "aexx", run.method.aexx);
				ReadValue(method, "precFock", run.method.precFock);
				ReadValue(method, "encut", run.method.encut);
				ReadValue(method, "prec", run.method.prec);
				ReadValue(method, "ispin", run.method.ispin);
				ReadValue(method, "potcars", run.method.potcars);
				run.method.prec = Lower(run.method.prec);
				run.method.precFock = Lower(run.method.precFock);
			}
			return run;
		}

		template <typename T>
		void EmitValue(YAML::Emitter &emit, const std::string &key, const T &value)
		{
			emit << YAML::Key << key << YAML::Value << value;
		}

		template <typename T>
		void EmitSequence(YAML::Emitter &emit, const std::string &key, const std::vector<T> &values)
		{
			emit << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
			for (const auto &value : values) emit << value;
			emit << YAML::EndSeq;
		}

		template <typename T>
		void EmitMap(YAML::Emitter &emit, const std::string &key, const std::map<std::string, T> &values)
		{
			emit << YAML::Key << key << YAML::Value << YAML::BeginMap;
			for (const auto &[species, value] : values) EmitValue(emit, species, value);
			emit << YAML::EndMap;
		}

		void EmitRun(YAML::Emitter &emit, const VaspRunRecord &run, const Path &project)
		{
			emit << YAML::BeginMap;
			EmitValue(emit, "directory", StoredPath(run.directory, project));
			EmitSequence(emit, "species", run.species);
			EmitSequence(emit, "ions", run.ions);
			EmitSequence(emit, "valence", run.valence);
			if (run.electrons) EmitValue(emit, "electrons", *run.electrons);
			if (run.totalEnergy) EmitValue(emit, "totalEnergy", *run.totalEnergy);
			EmitValue(emit, "finished", run.finished);
			EmitValue(emit, "relaxation", run.relaxation);
			EmitValue(emit, "ionicConverged", run.ionicConverged);
			EmitValue(emit, "hasLocpot", run.hasLocpot);
			EmitValue(emit, "hasContcar", run.hasContcar);
			EmitValue(emit, "outcarSize", run.outcarSize);
			EmitValue(emit, "outcarWriteTime", run.outcarWriteTime);
			if (!run.error.empty()) EmitValue(emit, "error", run.error);
			emit << YAML::Key << "method" << YAML::Value << YAML::BeginMap;
			EmitValue(emit, "hybrid", run.method.hybrid);
			EmitValue(emit, "hfscreen", run.method.hfscreen);
			EmitValue(emit, "aexx", run.method.aexx);
			EmitValue(emit, "precFock", Lower(run.method.precFock));
			EmitValue(emit, "encut", run.method.encut);
			EmitValue(emit, "prec", Lower(run.method.prec));
			EmitValue(emit, "ispin", run.method.ispin);
			EmitSequence(emit, "potcars", run.method.potcars);
			emit << YAML::EndMap << YAML::EndMap;
		}
	}

	Path EnergeticsIO::FilePath(const Path &projectDirectory)
	{
		return projectDirectory / "energetics.yaml";
	}

	bool EnergeticsIO::Parse(const std::string &text, const Path &projectDirectory,
		EnergeticsFile &outFile, std::vector<std::string> &outWarnings, std::string &outError)
	{
		outFile = {};
		outWarnings.clear();
		outError.clear();
		try
		{
			if (text.find_first_not_of(" \t\r\n") == std::string::npos) return true;
			const auto root = YAML::Load(text);
			RequireMap(root);
			EnergeticsFile file;
			const auto attempt = [&](const std::string &label, const auto &read) {
				try { read(); }
				catch (const std::exception &exception)
				{
					std::string warning = "Dropped " + label + ": " + exception.what();
					std::replace_if(warning.begin(), warning.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
					outWarnings.push_back(std::move(warning));
				}
			};
			attempt("formatVersion", [&] { ReadValue(root, "formatVersion", file.formatVersion); });
			if (file.formatVersion > kFormatVersion) throw std::invalid_argument("energetics.yaml uses a future format version");
			attempt("root", [&] { if (root["root"]) file.root = ReadPath(root["root"], projectDirectory); });
			attempt("bulkRun", [&] { ReadOptionalPath(root, "bulkRun", projectDirectory, file.bulkRun); });
			attempt("dielectric", [&] {
				double value = file.dielectric;
				ReadValue(root, "dielectric", value);
				if (value <= 0) throw std::invalid_argument("dielectric must be positive");
				file.dielectric = value;
			});
			attempt("dielectricSource", [&] { ReadValue(root, "dielectricSource", file.dielectricSource); });
			attempt("gap", [&] {
				std::optional<double> value;
				ReadOptional(root, "gap", value);
				if (value && *value < 0) throw std::invalid_argument("gap must be nonnegative");
				file.gap = value;
			});
			attempt("gapSource", [&] { ReadValue(root, "gapSource", file.gapSource); });
			const auto entries = [&](const std::string &key, const auto &read) {
				const auto nodes = root[key];
				if (!nodes) return;
				if (!nodes.IsSequence())
				{
					outWarnings.push_back("Dropped " + key + ": expected a sequence");
					return;
				}
				for (std::size_t i = 0; i < nodes.size(); ++i)
					attempt(key + " " + std::to_string(i), [&] { RequireMap(nodes[i]); read(nodes[i]); });
			};
			entries("runs", [&](const auto &node) { file.runs.push_back(ReadRun(node, projectDirectory)); });
			entries("choices", [&](const auto &node) {
				EnergeticsChoice choice;
				choice.defect = node["defect"].template as<std::string>();
				if (choice.defect.empty()) throw std::invalid_argument("empty defect name");
				choice.charge = node["charge"].template as<int>();
				ReadOptionalPath(node, "energyRun", projectDirectory, choice.energyRun);
				ReadOptionalPath(node, "locpot", projectDirectory, choice.locpot);
				ReadValue(node, "included", choice.included);
				file.choices.push_back(std::move(choice));
			});
			attempt("references", [&] {
				const auto refs = root["references"];
				if (!refs) return;
				RequireMap(refs);
				for (const auto &entry : refs)
					attempt("reference", [&] {
						const auto species = entry.first.as<std::string>();
						const auto path = ReadPath(entry.second, projectDirectory);
						if (species.empty() || path.Empty()) throw std::invalid_argument("empty reference species or path");
						file.references.emplace(species, path);
					});
			});
			entries("competing", [&](const auto &node) {
				EnergeticsPhase phase;
				phase.name = node["name"].template as<std::string>();
				phase.formula = ReadSpeciesMap<int>(node["formula"]);
				phase.run = ReadPath(node["run"], projectDirectory);
				if (phase.name.empty() || phase.formula.empty() || phase.run.Empty()) throw std::invalid_argument("empty competing phase");
				file.competing.push_back(std::move(phase));
			});
			entries("muSets", [&](const auto &node) {
				EnergeticsMuSet set;
				set.name = node["name"].template as<std::string>();
				if (set.name.empty()) throw std::invalid_argument("empty mu set name");
				set.deltaMu = ReadSpeciesMap<double>(node["deltaMu"]);
				file.muSets.push_back(std::move(set));
			});
			outFile = std::move(file);
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	std::string EnergeticsIO::Serialize(const EnergeticsFile &file, const Path &projectDirectory)
	{
		YAML::Emitter emit;
		emit << YAML::Precision(std::numeric_limits<double>::max_digits10) << YAML::BeginMap;
		EmitValue(emit, "formatVersion", kFormatVersion);
		EmitValue(emit, "root", StoredPath(file.root, projectDirectory));
		if (file.bulkRun) EmitValue(emit, "bulkRun", StoredPath(*file.bulkRun, projectDirectory));
		EmitValue(emit, "dielectric", file.dielectric);
		EmitValue(emit, "dielectricSource", file.dielectricSource);
		if (file.gap) EmitValue(emit, "gap", *file.gap);
		EmitValue(emit, "gapSource", file.gapSource);
		emit << YAML::Key << "runs" << YAML::Value << YAML::BeginSeq;
		for (const auto &run : file.runs) EmitRun(emit, run, projectDirectory);
		emit << YAML::EndSeq << YAML::Key << "choices" << YAML::Value << YAML::BeginSeq;
		for (const auto &choice : file.choices)
		{
			emit << YAML::BeginMap;
			EmitValue(emit, "defect", choice.defect);
			EmitValue(emit, "charge", choice.charge);
			if (choice.energyRun) EmitValue(emit, "energyRun", StoredPath(*choice.energyRun, projectDirectory));
			if (choice.locpot) EmitValue(emit, "locpot", StoredPath(*choice.locpot, projectDirectory));
			EmitValue(emit, "included", choice.included);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::Key << "references" << YAML::Value << YAML::BeginMap;
		for (const auto &[species, path] : file.references) EmitValue(emit, species, StoredPath(path, projectDirectory));
		emit << YAML::EndMap << YAML::Key << "competing" << YAML::Value << YAML::BeginSeq;
		for (const auto &phase : file.competing)
		{
			emit << YAML::BeginMap;
			EmitValue(emit, "name", phase.name);
			EmitMap(emit, "formula", phase.formula);
			EmitValue(emit, "run", StoredPath(phase.run, projectDirectory));
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::Key << "muSets" << YAML::Value << YAML::BeginSeq;
		for (const auto &set : file.muSets)
		{
			emit << YAML::BeginMap;
			EmitValue(emit, "name", set.name);
			EmitMap(emit, "deltaMu", set.deltaMu);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::EndMap;
		if (!emit.good()) throw std::runtime_error(emit.GetLastError());
		return emit.c_str();
	}

	bool EnergeticsIO::Load(const Path &projectDirectory, EnergeticsFile &outFile,
		std::vector<std::string> &outWarnings, std::string &outError)
	{
		outFile = {};
		outWarnings.clear();
		outError.clear();
		try
		{
			const Path path = FilePath(projectDirectory);
			if (!FileSystem::Exists(path.Native())) return true;
			std::string text;
			if (!TextFileIO::Load(path, text, outError)) return false;
			return Parse(text, projectDirectory, outFile, outWarnings, outError);
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	bool EnergeticsIO::Save(const Path &projectDirectory, const EnergeticsFile &file, std::string &outError)
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
			if (error) throw std::runtime_error(error.message());
			std::ofstream stream(temp.Native(), std::ios::binary | std::ios::trunc);
			stream.write(text.data(), static_cast<std::streamsize>(text.size()));
			stream.close();
			if (!stream) throw std::runtime_error("Unable to write " + temp.Utf8());
			if (!FileSystem::Rename(temp.Native(), target.Native(), error)) throw std::runtime_error(error.message());
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
}
