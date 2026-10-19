#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace DefectStudio
{
	struct PhononModePartner
	{
		int band = 0;
		double frequencyMeV = 0.0;
		std::vector<glm::dvec3> real, imag;
	};

	struct PhononModeField
	{
		std::string label;
		double frequencyMeV = 0.0;
		glm::dvec3 q{0.0};
		int band = 0, bandCount = 0;
		std::array<int, 3> supercell{1, 1, 1};
		bool commensurate = false;
		std::optional<std::array<int, 3>> suggestedSupercell;
		std::vector<std::string> symbols;
		std::vector<double> masses;
		std::array<glm::dvec3, 3> cell;
		std::vector<glm::dvec3> positions, real, imag;
		std::optional<PhononModePartner> partner;
	};
}
