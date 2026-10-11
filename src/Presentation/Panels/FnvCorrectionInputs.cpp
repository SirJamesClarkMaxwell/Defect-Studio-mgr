#include "Core/dspch.hpp"

#include "Presentation/Panels/FnvCorrectionInputs.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

namespace DefectStudio
{
	std::optional<int> ChargeFromCalculationFolder(const Path &locpot)
	{
		static const std::regex pattern("^(q|stan_)([+-]?)([0-9]+)([+-]?)$", std::regex::icase);
		const std::string folder = locpot.parent_path().filename().Utf8();
		std::smatch match;
		if (!std::regex_match(folder, match, pattern))
			return std::nullopt;
		const std::string prefix = match[1].str();
		const std::string before = match[2].str(), after = match[4].str();
		if ((!before.empty() && !after.empty()) || (prefix.size() == 1 && !after.empty()))
			return std::nullopt;
		const std::string sign = before.empty() ? after : before;
		const std::string number = (sign == "-" ? "-" : "") + match[3].str();
		int charge = 0;
		const auto parsed = std::from_chars(number.data(), number.data() + number.size(), charge);
		if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size())
			return std::nullopt;
		return charge;
	}

	std::optional<Path> FindBulkLocpotNear(const Path &defectLocpot)
	{
		Path ancestor = defectLocpot.parent_path();
		for (int depth = 2; depth <= 3; ++depth)
		{
			ancestor = ancestor.parent_path();
			const Path candidate = ancestor / Path::FromUtf8("bulk/LOCPOT");
			std::error_code error;
			if (std::filesystem::is_regular_file(candidate.Native(), error))
				return candidate;
		}
		return std::nullopt;
	}

	std::optional<glm::dvec3> FractionalCoordinates(const glm::mat3 &lattice, const glm::vec3 &cartesian)
	{
		const glm::dmat3 cell(lattice);
		const double determinant = glm::determinant(cell);
		if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-6)
			return std::nullopt;
		glm::dvec3 fractional = glm::inverse(cell) * glm::dvec3(cartesian);
		for (int axis = 0; axis < 3; ++axis)
		{
			if (!std::isfinite(fractional[axis]))
				return std::nullopt;
			fractional[axis] -= std::floor(fractional[axis]);
		}
		return fractional;
	}

	std::optional<VaspStructureHeader> ReadLocpotStructure(const Path &locpot)
	{
		std::error_code error;
		const Path contcar = locpot.parent_path() / Path::FromUtf8("CONTCAR");
		std::ifstream input((std::filesystem::is_regular_file(contcar.Native(), error) ? contcar : locpot).Native());
		std::string line;
		const auto next = [&]() { return static_cast<bool>(std::getline(input, line)); };
		double scale = 0.0;
		if (!next() || !next() || !(std::istringstream(line) >> scale) || !(scale > 0.0))
			return std::nullopt;
		VaspStructureHeader header;
		for (auto &vector : header.lattice)
			if (!next() || !(std::istringstream(line) >> vector.x >> vector.y >> vector.z))
				return std::nullopt;
		for (auto &vector : header.lattice)
			vector *= scale;

		std::vector<std::string> symbols;
		std::vector<int> counts;
		if (!next())
			return std::nullopt;
		std::istringstream symbolWords(line);
		for (std::string symbol; symbolWords >> symbol;)
			symbols.push_back(symbol.substr(0, symbol.find_first_of("_/"))); // POTCAR names: "Ge_d"
		if (!next())
			return std::nullopt;
		std::istringstream countWords(line);
		for (int count = 0; countWords >> count;)
			counts.push_back(count);
		// VASP 4 has no symbols line: its counts parse as "symbols" and "Direct" as no counts.
		if (symbols.empty() || symbols.size() != counts.size() ||
			!std::isalpha(static_cast<unsigned char>(symbols.front().front())))
			return std::nullopt;
		const auto firstLetter = [&]() {
			const std::size_t at = line.find_first_not_of(" \t");
			return at == std::string::npos ? '\0' : static_cast<char>(std::tolower(static_cast<unsigned char>(line[at])));
		};
		if (!next() || (firstLetter() == 's' && !next()) || firstLetter() != 'd') // [Selective dynamics] Direct
			return std::nullopt;
		for (std::size_t s = 0; s < symbols.size(); ++s)
			for (int i = 0; i < counts[s]; ++i)
			{
				glm::dvec3 position;
				if (!next() || !(std::istringstream(line) >> position.x >> position.y >> position.z))
					return std::nullopt;
				header.species.push_back(symbols[s]);
				header.fractional.push_back(position);
			}
		return header;
	}

	std::optional<ImpurityCentre> FindImpurityCentre(const VaspStructureHeader &defect, const VaspStructureHeader &bulk)
	{
		const std::set<std::string> host(bulk.species.begin(), bulk.species.end());
		ImpurityCentre centre;
		std::vector<glm::dvec3> picked;
		for (std::size_t i = 0; i < defect.species.size(); ++i)
		{
			if (host.contains(defect.species[i]))
				continue;
			picked.push_back(defect.fractional[i]);
			if (centre.species.find(defect.species[i]) == std::string::npos)
				centre.species += (centre.species.empty() ? "" : "+") + defect.species[i];
		}
		if (picked.empty())
			return std::nullopt;
		glm::dvec3 sum(0.0);
		for (const auto &position : picked)
		{
			const glm::dvec3 offset = position - picked.front();
			sum += offset - glm::round(offset);
		}
		centre.fractional = picked.front() + sum / static_cast<double>(picked.size());
		centre.fractional -= glm::floor(centre.fractional);
		return centre;
	}

	std::array<std::string, 3> FnvAxisLabels(const std::array<glm::dvec3, 3> &lattice)
	{
		std::array<std::string, 3> labels;
		for (int i = 0; i < 3; ++i)
		{
			labels[i] = "a" + std::to_string(i + 1);
			const double length = glm::length(lattice[i]);
			for (int c = 0; c < 3 && length > 0.0; ++c)
				if (std::abs(lattice[i][c]) / length > 0.9998) // cos 1 degree
					labels[i] += std::string(" (") + "xyz"[c] + ")";
		}
		return labels;
	}
} // namespace DefectStudio
