#include "Core/dspch.hpp"

#include "Presentation/Panels/FnvCorrectionInputs.hpp"

#include <charconv>
#include <cmath>
#include <regex>
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
} // namespace DefectStudio
