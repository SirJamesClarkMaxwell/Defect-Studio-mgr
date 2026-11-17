#include "Core/dspch.hpp"
#include "IO/PhononModeVectorIO.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <regex>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	std::vector<float> PhononModeVectorIO::ReadNpy(const Path &path, const std::vector<std::size_t> &expectedShape)
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
			shape.push_back(std::stoull(it->str()));
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
			if (!std::isfinite(value)) throw std::invalid_argument("Expected finite NPY values");
		}
		return data;
	}

	bool PhononModeVectorIO::Load(const Path &directory, PhononModeVector &out, std::string &error)
	{
		error.clear();
		try
		{
			std::string text;
			if (!TextFileIO::Load(directory / "mode.json", text, error)) return false;
			const auto data = nlohmann::json::parse(text);
			if (data.at("schema") != "ds.phonon_mode_vector.v1" || data.at("convention") != "normalised_mass_weighted")
				throw std::invalid_argument("Unsupported mode vector schema or convention");
			const auto count = [&](const char *key, int minimum) {
				const auto &value = data.at(key);
				if (!value.is_number_integer() || value.get<double>() < minimum ||
					value.get<double>() > std::numeric_limits<int>::max() / 3)
					throw std::invalid_argument("Invalid mode index or atom count");
				return value.get<std::size_t>();
			};
			const auto atoms = count("n_atoms", 1);
			PhononModeVector result;
			result.modeIndex = count("mode_index", 0);
			if (result.modeIndex >= atoms * 3) throw std::invalid_argument("Mode index out of range");
			result.frequencyMeV = data.at("frequency_meV").get<double>();
			if (!data.at("frequency_meV").is_number() || !std::isfinite(result.frequencyMeV))
				throw std::invalid_argument("Invalid mode frequency");
			result.eigenvector = ReadNpy(directory / "eigenvector.npy", {atoms, 3});
			double total = 0.0;
			for (std::size_t atom = 0; atom < atoms; ++atom)
			{
				double weight = 0.0;
				for (std::size_t axis = 0; axis < 3; ++axis)
				{
					const double value = result.eigenvector[atom * 3 + axis];
					weight += value * value;
				}
				total += weight;
				result.participation.push_back(static_cast<float>(weight));
			}
			if (std::abs(total - 1.0) > 1e-5) throw std::invalid_argument("Mode eigenvector must have unit norm");
			out = std::move(result);
			return true;
		}
		catch (const std::exception &exception) { error = exception.what(); return false; }
	}

	bool PhononModeVectorIO::EmbeddedDirectory(const Path &dataset, Path &out, std::string &error)
	{
		error.clear();
		try
		{
			std::string text;
			if (!TextFileIO::Load(dataset / "summary.json", text, error)) return false;
			const auto summary = nlohmann::json::parse(text);
			auto hr = Path::FromUtf8(summary.at("server").at("hr_dir").get<std::string>());
			if (summary.contains("inputs_relative") && summary.at("inputs_relative").contains("hr_dir") &&
				!summary.at("inputs_relative").at("hr_dir").is_null())
				hr = dataset / Path::FromUtf8(summary.at("inputs_relative").at("hr_dir").get<std::string>());
			else if (hr.Native().is_relative()) hr = dataset / hr;
			const auto size = hr.parent_path();
			if (hr.Empty() || size.parent_path().filename().Utf8() != "spectral-function")
				throw std::invalid_argument("Expected server spectral-function/AxBxC/variant layout");
			out = size.parent_path().parent_path() / "embedded" / size.filename();
			return true;
		}
		catch (const std::exception &exception) { error = exception.what(); return false; }
	}
}
