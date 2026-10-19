#include "Core/dspch.hpp"
#include "IO/PhononModeIO.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "IO/TextFileIO.hpp"

namespace DefectStudio::PhononModeIO
{
	namespace
	{
		double Number(const nlohmann::json &value)
		{
			if (!value.is_number() || !std::isfinite(value.get<double>()))
				throw std::invalid_argument("Expected a finite number in vibration.json");
			return value.get<double>();
		}

		int Integer(const nlohmann::json &value, int minimum)
		{
			if (!value.is_number_integer() || Number(value) < minimum || Number(value) > std::numeric_limits<int>::max())
				throw std::invalid_argument("Invalid integer in vibration.json");
			return value.get<int>();
		}

		glm::dvec3 Vector(const nlohmann::json &value)
		{
			if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Expected a three-vector");
			return {Number(value[0]), Number(value[1]), Number(value[2])};
		}

		std::array<int, 3> Size(const nlohmann::json &value)
		{
			if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Invalid supercell size");
			return {Integer(value[0], 1), Integer(value[1], 1), Integer(value[2], 1)};
		}

		std::vector<glm::dvec3> Vectors(const nlohmann::json &value, std::size_t count)
		{
			if (!value.is_array() || value.size() != count) throw std::invalid_argument("Per-atom length mismatch");
			std::vector<glm::dvec3> result;
			for (const auto &row : value) result.push_back(Vector(row));
			return result;
		}
	}

	bool Load(const Path &directory, PhononModeField &out, std::string &error)
	{
		error.clear();
		try
		{
			std::string text;
			if (!TextFileIO::Load(directory / "vibration.json", text, error)) return false;
			const auto data = nlohmann::json::parse(text);
			if (data.at("schema") != "ds.phonon_mode.v1" || data.at("q_basis") != "primitive_reciprocal_reduced")
				throw std::invalid_argument("Unsupported bulk mode schema or q basis");
			PhononModeField result;
			result.label = data.at("label").get<std::string>();
			result.frequencyMeV = Number(data.at("frequency_meV"));
			result.q = Vector(data.at("q"));
			result.band = Integer(data.at("band"), 0);
			result.bandCount = Integer(data.at("n_bands"), 1);
			if (result.band >= result.bandCount) throw std::invalid_argument("Band is outside n_bands");
			result.supercell = Size(data.at("supercell"));
			result.commensurate = data.at("commensurate").get<bool>();
			if (!data.at("suggested_supercell").is_null()) result.suggestedSupercell = Size(data.at("suggested_supercell"));
			result.symbols = data.at("symbols").get<std::vector<std::string>>();
			const auto atoms = result.symbols.size();
			if (atoms == 0) throw std::invalid_argument("Empty bulk mode");
			for (const auto &symbol : result.symbols)
				if (symbol.empty()) throw std::invalid_argument("Empty atom symbol");
			const auto &masses = data.at("masses_amu");
			if (!masses.is_array() || masses.size() != atoms) throw std::invalid_argument("Mass count mismatch");
			for (const auto &mass : masses)
			{
				const double value = Number(mass);
				if (value <= 0.0) throw std::invalid_argument("Mass must be positive");
				result.masses.push_back(value);
			}
			const auto cell = Vectors(data.at("cell_angstrom"), 3);
			std::copy(cell.begin(), cell.end(), result.cell.begin());
			result.positions = Vectors(data.at("positions_angstrom"), atoms);
			result.real = Vectors(data.at("real_angstrom"), atoms);
			result.imag = Vectors(data.at("imag_angstrom"), atoms);
			if (!data.at("partner").is_null())
			{
				const auto &partner = data.at("partner");
				result.partner = PhononModePartner{Integer(partner.at("band"), 0), Number(partner.at("frequency_meV")),
					Vectors(partner.at("real_angstrom"), atoms), Vectors(partner.at("imag_angstrom"), atoms)};
				if (result.partner->band >= result.bandCount || result.partner->band == result.band)
					throw std::invalid_argument("Invalid partner band");
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
