#include "Core/dspch.hpp"
#include "IO/CcdAnalysisIO.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <nlohmann/json.hpp>
#include "IO/TextFileIO.hpp"

namespace DefectStudio::CcdAnalysisIO
{
	namespace
	{
		double Finite(const nlohmann::json &value)
		{
			if (!value.is_number()) throw std::invalid_argument("expected a finite number");
			const double number = value.get<double>();
			if (!std::isfinite(number)) throw std::invalid_argument("non-finite number");
			return number;
		}

		double Number(const std::string &text, bool allowMissing = false)
		{
			if (allowMissing && (text.empty() || text == "nan" || text == "NaN"))
				return std::numeric_limits<double>::quiet_NaN();
			std::size_t consumed = 0;
			const double value = std::stod(text, &consumed);
			if (consumed != text.size() || !std::isfinite(value)) throw std::invalid_argument("invalid numeric cell: " + text);
			return value;
		}

		std::size_t Index(const std::string &text)
		{
			std::size_t value = 0;
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
				throw std::invalid_argument("invalid level index");
			return value;
		}

		std::vector<std::string> Columns(std::string line)
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			std::vector<std::string> result;
			std::size_t start = 0, end;
			do
			{
				end = line.find('\t', start);
				result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
				start = end + 1;
			} while (end != std::string::npos);
			return result;
		}

		struct Table
		{
			std::vector<std::string> header;
			std::vector<std::vector<std::string>> rows;
			std::size_t Column(const std::string &name) const
			{
				const auto it = std::find(header.begin(), header.end(), name);
				if (it == header.end()) throw std::invalid_argument("missing column " + name);
				return static_cast<std::size_t>(it - header.begin());
			}
		};

		std::optional<Table> ReadTable(const Path &directory, const std::string &file)
		{
			std::error_code error;
			const auto path = directory / file;
			const bool exists = std::filesystem::exists(path.Native(), error);
			if (error) throw std::runtime_error(error.message());
			if (!exists) return std::nullopt;
			std::string text, message;
			if (!TextFileIO::Load(path, text, message)) throw std::runtime_error(message);
			Table table;
			std::istringstream input(text);
			std::string line;
			if (!std::getline(input, line)) throw std::invalid_argument("missing TSV header");
			table.header = Columns(line);
			for (std::size_t i = 0; i < table.header.size(); ++i)
				if (table.header[i].empty() || std::find(table.header.begin(), table.header.begin() + i, table.header[i]) != table.header.begin() + i)
					throw std::invalid_argument("empty or duplicate column");
			while (std::getline(input, line))
			{
				if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
				auto row = Columns(line);
				if (row.size() != table.header.size()) throw std::invalid_argument("wrong row column count");
				table.rows.push_back(std::move(row));
			}
			return table;
		}

		void AppendGrid(std::vector<double> &grid, double value)
		{
			if (!grid.empty() && value <= grid.back()) throw std::invalid_argument("grid must be strictly increasing");
			grid.push_back(value);
		}

		void ReadSummary(const nlohmann::json &summary, CcdAnalysisResult &result)
		{
			if (summary.at("schema") != "ds.ccd_analysis.v1") throw std::invalid_argument("unsupported CCD schema");
			for (const char *key : {"harmonic", "franck_condon", "zpl_formula", "numerov"})
				if (summary.contains(key) && !summary.at(key).is_null() && !summary.at(key).is_object())
					throw std::invalid_argument(std::string(key) + " must be an object or null");
			result.label = summary.value("label", "");
			result.deltaQ = Finite(summary.at("delta_Q_amu05_A"));
			result.zplEv = Finite(summary.at("zpl_eV").at("used"));
			if (result.zplEv <= 0) throw std::invalid_argument("ZPL must be positive");
			if (summary.contains("zpl_formula") && !summary.at("zpl_formula").is_null())
				result.zplFormulaText = summary.at("zpl_formula").at("text").get<std::string>();
			if (summary.contains("harmonic") && !summary.at("harmonic").is_null())
			{
				for (const auto branch : {"gs", "es"})
				{
					const auto &harmonic = summary.at("harmonic");
					if (!harmonic.contains(branch) || harmonic.at(branch).is_null()) continue;
					const auto &well = harmonic.at(branch);
					CcdHarmonicWell value{Finite(well.at("hbar_omega_meV")), Finite(well.at("S"))};
					if (value.hbarOmegaMeV <= 0 || value.huangRhys < 0) throw std::invalid_argument("invalid harmonic well");
					(std::string_view(branch) == "gs" ? result.harmonic.gs : result.harmonic.es) = value;
				}
			}
			if (summary.contains("franck_condon") && !summary.at("franck_condon").is_null())
			{
				const auto &fc = summary.at("franck_condon");
				result.franckCondon.absorptionEv = Finite(fc.at("absorption_eV"));
				result.franckCondon.emissionEv = Finite(fc.at("emission_eV"));
				result.franckCondon.relaxationGsEv = Finite(fc.at("relaxation_gs_eV"));
				result.franckCondon.relaxationEsEv = Finite(fc.at("relaxation_es_eV"));
				result.franckCondon.source = fc.at("source").get<std::string>();
			}
			if (summary.contains("numerov") && !summary.at("numerov").is_null() && summary.at("numerov").contains("fc_0n"))
			{
				const auto &weights = summary.at("numerov").at("fc_0n");
				if (!weights.is_array()) throw std::invalid_argument("fc_0n must be an array");
				for (const auto &item : weights)
				{
					const double weight = Finite(item);
					if (weight < 0) throw std::invalid_argument("negative FC weight");
					result.fc0n.push_back(weight);
				}
			}
		}
	}

	bool Load(const Path &directory, CcdAnalysisResult &out, std::string &error)
	{
		error.clear();
		std::string file = "summary.json";
		try
		{
			std::string text;
			if (!TextFileIO::Load(directory / file, text, error)) { error = file + ": " + error; return false; }
			CcdAnalysisResult result;
			ReadSummary(nlohmann::json::parse(text), result);
			file = "ccd_points.tsv";
			if (const auto table = ReadTable(directory, file))
			{
				const auto b = table->Column("branch"), t = table->Column("t"), q = table->Column("Q_amu05_A"),
					e = table->Column("energy_eV"), s = table->Column("status");
				for (const auto &row : table->rows)
				{
					if ((row[b] != "gs" && row[b] != "es" && row[b] != "triplet") || row[s].empty())
						throw std::invalid_argument("invalid point branch/status");
					CcdPoint point{row[b], Number(row[t]), Number(row[q]), std::nullopt, row[s]};
					const double energy = Number(row[e], true);
					if (std::isfinite(energy)) point.energyEv = energy;
					else if (point.status == "ok") throw std::invalid_argument("ok point has no energy");
					result.points.push_back(std::move(point));
				}
			}
			file = "ccd_curves.tsv";
			if (const auto table = ReadTable(directory, file))
			{
				const auto q = table->Column("Q_amu05_A"), gs = table->Column("V_gs_eV"), es = table->Column("V_es_eV");
				for (const auto &row : table->rows)
				{
					AppendGrid(result.curves.q, Number(row[q]));
					result.curves.vGsEv.push_back(Number(row[gs]));
					result.curves.vEsEv.push_back(Number(row[es]));
				}
			}
			file = "levels.tsv";
			if (const auto table = ReadTable(directory, file))
			{
				const auto b = table->Column("branch"), n = table->Column("n"), e = table->Column("energy_meV");
				for (const auto &row : table->rows)
				{
					if (row[b] != "gs" && row[b] != "es") throw std::invalid_argument("invalid level branch");
					auto &levels = row[b] == "gs" ? result.levels.gsMeV : result.levels.esMeV;
					const double energy = Number(row[e]);
					if (Index(row[n]) != levels.size() || energy < 0 || (!levels.empty() && energy <= levels.back()))
						throw std::invalid_argument("levels must be consecutive, increasing and nonnegative");
					levels.push_back(energy);
				}
			}
			file = "wavefunctions.tsv";
			if (const auto table = ReadTable(directory, file))
			{
				const auto q = table->Column("Q_amu05_A");
				for (const auto branch : {"gs", "es"})
				{
					auto &waves = std::string_view(branch) == "gs" ? result.wavefunctions.gs : result.wavefunctions.es;
					const auto &levels = std::string_view(branch) == "gs" ? result.levels.gsMeV : result.levels.esMeV;
					std::map<std::size_t, std::size_t> columns;
					const std::string prefix = std::string(branch) + "_";
					for (std::size_t c = 0; c < table->header.size(); ++c)
						if (table->header[c].starts_with(prefix) && !columns.emplace(Index(table->header[c].substr(prefix.size())), c).second)
							throw std::invalid_argument("duplicate wavefunction level");
					for (const auto &[n, c] : columns)
					{
						if (n != waves.size() || (!levels.empty() && n >= levels.size())) throw std::invalid_argument("wavefunction columns skip a level");
						std::vector<double> values;
						for (const auto &row : table->rows) values.push_back(Number(row[c]));
						waves.push_back(std::move(values));
					}
				}
				for (const auto &name : table->header)
					if (name != "Q_amu05_A" && !name.starts_with("gs_") && !name.starts_with("es_"))
						throw std::invalid_argument("unknown wavefunction column");
				for (const auto &row : table->rows) AppendGrid(result.wavefunctions.q, Number(row[q]));
			}
			file = "pl_1d.tsv";
			if (const auto table = ReadTable(directory, file))
			{
				const auto e = table->Column("photon_energy_eV");
				for (const auto &row : table->rows) AppendGrid(result.pl1d.energyEv, Number(row[e]));
				const auto readCurve = [&](const char *name, std::vector<double> &values) {
					const auto c = table->Column(name);
					for (const auto &row : table->rows)
					{
						const double value = Number(row[c], true);
						if (value < 0) throw std::invalid_argument("negative PL intensity");
						values.push_back(value);
					}
					if (std::none_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); })) values.clear();
				};
				readCurve("intensity_numerov", result.pl1d.numerov);
				readCurve("intensity_harmonic", result.pl1d.harmonic);
				readCurve("intensity_phonon_1d", result.pl1d.phonon1d);
			}
			out = std::move(result);
			return true;
		}
		catch (const std::exception &exception)
		{
			error = file + ": " + exception.what();
			return false;
		}
	}
}
