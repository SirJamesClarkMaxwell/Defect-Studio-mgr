#include "Core/dspch.hpp"

#include "IO/PhononAnalysisIO.hpp"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <regex>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string ReadText(const Path &path)
		{
			std::string text, error;
			if (!TextFileIO::Load(path, text, error))
				throw std::runtime_error(error);
			return text;
		}

		template <typename Number>
		Number ParseNumber(const std::string &text)
		{
			Number value{};
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
				throw std::invalid_argument("Invalid number: " + text);
			return value;
		}

		double Finite(double value)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("Expected a finite number");
			return value;
		}

		std::vector<std::string> SplitRow(const std::string &line)
		{
			std::vector<std::string> fields;
			std::size_t begin = 0;
			for (;;)
			{
				const auto end = line.find('\t', begin);
				fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
				if (end == std::string::npos)
					return fields;
				begin = end + 1;
			}
		}

		template <typename Consumer>
		void ReadTable(const Path &path, const std::string &header, Consumer consume, const std::string &alternateHeader = {})
		{
			std::istringstream stream(ReadText(path));
			std::string line;
			auto readLine = [&]() {
				if (!std::getline(stream, line))
					return false;
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				return true;
			};
			if (!readLine() || (line != header && (alternateHeader.empty() || line != alternateHeader)))
				throw std::invalid_argument("Invalid table header");
			const auto columns = SplitRow(line).size();
			while (readLine())
			{
				if (line.empty())
					continue;
				const auto fields = SplitRow(line);
				if (fields.size() != columns)
					throw std::invalid_argument("Invalid table column count");
				consume(fields);
			}
		}

		std::vector<float> ReadNpy(const Path &path, const std::vector<std::size_t> &expectedShape)
		{
			std::ifstream stream(path.Native(), std::ios::binary | std::ios::ate);
			if (!stream)
				throw std::runtime_error("Unable to open NPY file");
			const std::streamoff fileSize = stream.tellg();
			stream.seekg(0);
			std::array<unsigned char, 8> prefix{};
			stream.read(reinterpret_cast<char *>(prefix.data()), prefix.size());
			const std::array<unsigned char, 6> magic{0x93, 'N', 'U', 'M', 'P', 'Y'};
			if (!stream || !std::equal(magic.begin(), magic.end(), prefix.begin()) || prefix[7] != 0 ||
				(prefix[6] != 1 && prefix[6] != 2))
				throw std::invalid_argument("Expected an NPY v1.0 or v2.0 file");
			const int lengthBytes = prefix[6] == 1 ? 2 : 4;
			std::array<unsigned char, 4> bytes{};
			stream.read(reinterpret_cast<char *>(bytes.data()), lengthBytes);
			std::uint32_t headerLength = 0;
			for (int i = 0; i < lengthBytes; ++i)
				headerLength |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
			if (!stream || headerLength == 0 || static_cast<std::streamoff>(headerLength) > fileSize - (8 + lengthBytes))
				throw std::invalid_argument("Truncated NPY header");
			std::string header(headerLength, '\0');
			stream.read(header.data(), header.size());
			if (!stream || header.back() != '\n')
				throw std::invalid_argument("Invalid NPY header");

			// Parse only the three literal fields of the NPY dictionary, in any order; never evaluate Python.
			static const std::regex dictionary(R"(^\s*\{([\s\S]*)\}\s*$)");
			static const std::regex field(R"(\s*(['"])(descr|fortran_order|shape)\1\s*:\s*(['"][^'"]*['"]|True|False|\([^)]*\))\s*(,|$))");
			std::smatch match;
			if (!std::regex_match(header, match, dictionary))
				throw std::invalid_argument("Invalid NPY dictionary");
			std::string remaining = match[1];
			std::map<std::string, std::string> fields;
			while (remaining.find_first_not_of(" \t\r\n") != std::string::npos)
			{
				if (!std::regex_search(remaining, match, field, std::regex_constants::match_continuous) ||
					!fields.emplace(match[2].str(), match[3].str()).second)
					throw std::invalid_argument("Invalid or duplicate NPY field");
				remaining = match.suffix();
			}
			if (fields.size() != 3 || (fields.at("descr") != "'<f4'" && fields.at("descr") != "\"<f4\"") ||
				fields.at("fortran_order") != "False")
				throw std::invalid_argument("Expected little-endian float32 C-order NPY data");
			const auto &shapeText = fields.at("shape");
			static const std::regex shapePattern(R"(^\(\s*[0-9]+\s*(,\s*[0-9]+\s*)*,?\s*\)$)");
			if (!std::regex_match(shapeText, shapePattern))
				throw std::invalid_argument("Invalid NPY shape");
			static const std::regex dimension(R"([0-9]+)");
			std::vector<std::size_t> shape;
			for (auto it = std::sregex_iterator(shapeText.begin(), shapeText.end(), dimension);
				it != std::sregex_iterator(); ++it)
				shape.push_back(ParseNumber<std::size_t>(it->str()));
			if (shape != expectedShape)
				throw std::invalid_argument("NPY shape does not match modes and atoms");
			std::size_t count = 1;
			for (const auto size : shape)
			{
				if (size == 0 || count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(float) / size)
					throw std::invalid_argument("NPY shape is too large or empty");
				count *= size;
			}
			const auto dataBytes = static_cast<std::streamsize>(count * sizeof(float));
			if (fileSize - static_cast<std::streamoff>(stream.tellg()) != dataBytes)
				throw std::invalid_argument("NPY payload length does not match shape");
			std::vector<float> data(count);
			stream.read(reinterpret_cast<char *>(data.data()), dataBytes);
			if (!stream)
				throw std::runtime_error("Unable to read NPY payload");
			for (auto &value : data)
			{
				if constexpr (std::endian::native == std::endian::big)
					value = std::bit_cast<float>(std::byteswap(std::bit_cast<std::uint32_t>(value)));
				(void)Finite(value);
			}
			return data;
		}

		glm::dvec3 ReadVector(const nlohmann::json &values)
		{
			if (!values.is_array() || values.size() != 3)
				throw std::invalid_argument("Expected three coordinates");
			return {Finite(values.at(0).get<double>()), Finite(values.at(1).get<double>()), Finite(values.at(2).get<double>())};
		}

		std::size_t ReadCount(const nlohmann::json &value)
		{
			if (!value.is_number_integer() || value.get<std::int64_t>() <= 0)
				throw std::invalid_argument("Expected a positive integer count");
			return value.get<std::size_t>();
		}
	} // namespace

	bool PhononAnalysisIO::Load(const Path &directory, PhononAnalysisResult &outResult, std::string &outError)
	{
		outError.clear();
		std::string currentFile = "summary.json";
		try
		{
			PhononAnalysisResult result;
			result.directory = directory;
			const auto summary = nlohmann::json::parse(ReadText(directory / currentFile));
			if (summary.at("schema").get<std::string>() != kSchema)
				throw std::invalid_argument("Unsupported phonon analysis schema");
			const auto atoms = ReadCount(summary.at("n_atoms"));
			const auto modes = ReadCount(summary.at("n_modes"));
			if (atoms > static_cast<std::size_t>(std::numeric_limits<int>::max()) / 3 || modes != atoms * 3)
				throw std::invalid_argument("Expected 3 modes per atom");
			result.sTotal = Finite(summary.at("s_total").get<double>());
			result.debyeWaller = Finite(summary.at("debye_waller").get<double>());
			result.stable = summary.at("stable").get<bool>();
			const auto &imaginary = summary.at("imaginary_modes");
			if (!imaginary.is_array())
				throw std::invalid_argument("imaginary_modes must be an array");
			for (const auto &entry : imaginary)
			{
				const int index = entry.at("index").get<int>();
				if (index < 0 || static_cast<std::size_t>(index) >= modes)
					throw std::invalid_argument("Imaginary mode index is out of range");
				result.imaginaryModes.push_back(index);
			}
			const auto &zpl = summary.at("zpl_eV");
			auto readZpl = [&](const std::string &key) -> std::optional<double> {
				return zpl.at(key).is_null() ? std::nullopt : std::optional<double>(Finite(zpl.at(key).get<double>()));
			};
			result.zplEv = {readZpl("computed"), readZpl("computed_spin_purified"), readZpl("experimental")};
			if (zpl.contains("formula"))
				result.zplEv.formula = readZpl("formula");
			if (summary.contains("zpl_formula") && !summary.at("zpl_formula").is_null())
				result.zplFormulaText = summary.at("zpl_formula").at("text").get<std::string>();
			if (summary.contains("label"))
				result.label = summary.at("label").get<std::string>();
			result.sigmaMeV = Finite(summary.at("sigma_meV").get<double>());
			result.gammaMeV = Finite(summary.at("gamma_meV").get<double>());
			result.maxDisplacementAngstrom = Finite(summary.at("max_displacement_A").get<double>());
			result.deltaQAmuHalfAngstrom = Finite(summary.at("delta_Q_amu05_A").get<double>());
			result.approximation = summary.at("approximation").get<std::string>();

			currentFile = "structure.json";
			const auto structure = nlohmann::json::parse(ReadText(directory / currentFile));
			result.symbols = structure.at("symbols").get<std::vector<std::string>>();
			result.massesAmu = structure.at("masses_amu").get<std::vector<double>>();
			const auto &positions = structure.at("positions_A");
			if (result.symbols.size() != atoms || result.massesAmu.size() != atoms ||
				!positions.is_array() || positions.size() != atoms)
				throw std::invalid_argument("Structure arrays must have n_atoms entries");
			for (const auto mass : result.massesAmu)
				if (!std::isfinite(mass) || mass <= 0.0)
					throw std::invalid_argument("Atomic masses must be finite and positive");
			for (const auto &position : positions)
				result.positionsAngstrom.push_back(ReadVector(position));
			const auto &cell = structure.at("cell_A");
			if (!cell.is_array() || cell.size() != 3)
				throw std::invalid_argument("Cell must have three lattice vectors");
			for (int column = 0; column < 3; ++column)
				result.cellAngstrom[column] = ReadVector(cell.at(column));

			currentFile = "modes.tsv";
			ReadTable(directory / currentFile, "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation",
				[&](const auto &row) {
					PhononMode mode;
					mode.index = ParseNumber<int>(row[0]);
					if (result.modes.size() >= modes || mode.index != static_cast<int>(result.modes.size()))
						throw std::invalid_argument("Mode indices must be consecutive from zero");
					mode.frequencyMeV = Finite(ParseNumber<double>(row[1]));
					mode.irrep = row[2];
					mode.effectiveAtomCount = Finite(ParseNumber<double>(row[3]));
					mode.beta = Finite(ParseNumber<double>(row[4]));
					mode.huangRhys = Finite(ParseNumber<double>(row[5]));
					mode.qAmuHalfAngstrom = Finite(ParseNumber<double>(row[6]));
					if (row[7] != "0" && row[7] != "1")
						throw std::invalid_argument("is_translation must be 0 or 1");
					mode.isTranslation = row[7] == "1";
					result.modes.push_back(std::move(mode));
				});
			if (result.ModeCount() != modes)
				throw std::invalid_argument("modes.tsv must contain 3 * n_atoms rows");
			currentFile = "eigenvectors.npy";
			result.eigenvectors = ReadNpy(directory / currentFile, {modes, atoms, 3});
			currentFile = "participation.npy";
			result.participation = ReadNpy(directory / currentFile, {modes, atoms});
			if (std::any_of(result.participation.begin(), result.participation.end(), [](float value) { return value < 0.0f; }))
				throw std::invalid_argument("Participation must be nonnegative");

			currentFile = "spectral_function.tsv";
			if (FileSystem::Exists((directory / currentFile).Native()))
				ReadTable(directory / currentFile, "energy_meV\tS_E_per_meV", [&](const auto &row) {
					result.spectralEnergyMeV.push_back(Finite(ParseNumber<double>(row[0])));
					result.spectralDensityPerMeV.push_back(Finite(ParseNumber<double>(row[1])));
				});
			currentFile = "pl.tsv";
			if (FileSystem::Exists((directory / currentFile).Native()))
			{
				ReadTable(directory / currentFile,
					"photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental", [&](const auto &row) {
						result.plPhotonEnergyEv.push_back(Finite(ParseNumber<double>(row[0])));
						result.plComputed.push_back(ParseNumber<double>(row[1]));
						result.plSpinPurified.push_back(ParseNumber<double>(row[2]));
						result.plExperimental.push_back(ParseNumber<double>(row[3]));
						if (row.size() == 5)
							result.plFormula.push_back(ParseNumber<double>(row[4]));
					}, "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula");
				for (auto &column : {std::ref(result.plComputed), std::ref(result.plSpinPurified), std::ref(result.plExperimental),
					std::ref(result.plFormula)})
				{
					if (std::all_of(column.get().begin(), column.get().end(), [](double value) { return std::isnan(value); }))
						column.get().clear();
					else
						for (const auto value : column.get())
							(void)Finite(value);
				}
			}
			outResult = std::move(result);
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = currentFile + ": " + exception.what();
			return false;
		}
	}
} // namespace DefectStudio
