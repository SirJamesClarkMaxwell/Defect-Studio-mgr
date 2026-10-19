#include "Core/dspch.hpp"
#include "IO/PhononDispersionIO.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "IO/TextFileIO.hpp"

namespace DefectStudio::PhononDispersionIO
{
	namespace
	{
		double Finite(const nlohmann::json &value)
		{
			if (!value.is_number()) throw std::invalid_argument("Expected a finite number in summary.json");
			const double number = value.get<double>();
			if (!std::isfinite(number)) throw std::invalid_argument("Non-finite value in summary.json");
			return number;
		}

		std::vector<std::string> Columns(std::string line)
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			std::vector<std::string> columns;
			std::size_t start = 0, end;
			do
			{
				end = line.find('\t', start);
				columns.push_back(line.substr(start, end == std::string::npos ? end : end - start));
				start = end + 1;
			} while (end != std::string::npos);
			return columns;
		}

		double Number(const std::string &text)
		{
			std::size_t consumed = 0;
			const double number = std::stod(text, &consumed);
			if (consumed != text.size() || !std::isfinite(number))
				throw std::invalid_argument("Invalid or non-finite number in dispersion.tsv");
			return number;
		}
	}

	bool Load(const Path &directory, PhononDispersion &out, std::string &error)
	{
		error.clear();
		try
		{
			std::string summaryText, tableText;
			if (!TextFileIO::Load(directory.Join("summary.json"), summaryText, error) ||
				!TextFileIO::Load(directory.Join("dispersion.tsv"), tableText, error)) return false;
			const auto summary = nlohmann::json::parse(summaryText);
			if (summary.at("schema") != "ds.phonon_dispersion.v1")
				throw std::invalid_argument("Unsupported phonon dispersion schema");
			const auto &count = summary.at("n_bands");
			if (!count.is_number_integer() || Finite(count) < 1 || Finite(count) > std::numeric_limits<int>::max() - 2)
				throw std::invalid_argument("Invalid phonon band count");
			PhononDispersion result;
			result.bandCount = count.get<int>();
			result.minMeV = Finite(summary.at("min_freq_meV"));
			result.maxMeV = Finite(summary.at("max_freq_meV"));
			if (result.minMeV > result.maxMeV) throw std::invalid_argument("Reversed phonon frequency range");
			result.inPlane = summary.at("in_plane").get<bool>();
			if (!summary.at("ticks").is_array() || summary.at("ticks").empty() || !summary.at("breaks").is_array())
				throw std::invalid_argument("Invalid phonon path ticks or breaks");
			for (const auto &tick : summary.at("ticks"))
			{
				PhononDispersionTick value{Finite(tick.at("distance")), tick.at("label").get<std::string>()};
				if (value.distance < 0 || value.label.empty() ||
					(!result.ticks.empty() && value.distance < result.ticks.back().distance))
					throw std::invalid_argument("Invalid phonon path tick");
				result.ticks.push_back(std::move(value));
			}
			for (const auto &item : summary.at("breaks"))
			{
				const double value = Finite(item);
				if (value < 0 || (!result.breaks.empty() && value < result.breaks.back()))
					throw std::invalid_argument("Invalid phonon path break");
				result.breaks.push_back(value);
			}

			std::istringstream table(tableText);
			std::string line;
			std::getline(table, line);
			const auto header = Columns(line);
			const auto columnCount = static_cast<std::size_t>(result.bandCount) + 2;
			if (header.size() != columnCount || header[0] != "segment" || header[1] != "distance")
				throw std::invalid_argument("Invalid dispersion.tsv header column count or names");
			for (int band = 0; band < result.bandCount; ++band)
				if (header[band + 2] != "band_" + std::to_string(band))
					throw std::invalid_argument("Invalid dispersion.tsv band column");
			result.bandsMeV.resize(result.bandCount);
			while (std::getline(table, line))
			{
				if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
				const auto columns = Columns(line);
				if (columns.size() != columnCount) throw std::invalid_argument("Invalid dispersion.tsv row column count");
				int segment = 0;
				const auto parsed = std::from_chars(columns[0].data(), columns[0].data() + columns[0].size(), segment);
				if (parsed.ec != std::errc{} || parsed.ptr != columns[0].data() + columns[0].size() || segment < 0 ||
					(!result.segment.empty() && segment < result.segment.back()))
					throw std::invalid_argument("Invalid dispersion.tsv segment");
				const double distance = Number(columns[1]);
				if (distance < 0 || (!result.distance.empty() && distance < result.distance.back()))
					throw std::invalid_argument("Phonon path distances must be non-decreasing");
				result.segment.push_back(segment);
				result.distance.push_back(distance);
				for (int band = 0; band < result.bandCount; ++band)
					result.bandsMeV[band].push_back(Number(columns[band + 2]));
			}
			if (result.distance.empty()) throw std::invalid_argument("Empty dispersion.tsv");
			if (FileSystem::Exists((directory / "qpoints.tsv").Native()))
			{
				std::string qText;
				if (!TextFileIO::Load(directory / "qpoints.tsv", qText, error)) return false;
				std::istringstream qTable(qText);
				std::getline(qTable, line);
				if (Columns(line) != std::vector<std::string>{"qx", "qy", "qz"})
					throw std::invalid_argument("Invalid qpoints.tsv header");
				while (std::getline(qTable, line))
				{
					if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
					const auto columns = Columns(line);
					if (columns.size() != 3) throw std::invalid_argument("Invalid qpoints.tsv row");
					result.qpoints.emplace_back(Number(columns[0]), Number(columns[1]), Number(columns[2]));
				}
				if (result.qpoints.size() != result.distance.size()) throw std::invalid_argument("qpoints.tsv row count mismatch");
			}
			out = std::move(result);
			return true;
		}
		catch (const std::exception &exception)
		{
			error = exception.what();
			return false;
		}
	}
}
