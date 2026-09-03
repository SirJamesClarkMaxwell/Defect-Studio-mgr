#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct SiteDefinition
	{
		std::string name; // "A", "B", etc
		// Every position this site occupies in the CONVENTIONAL cell, fractional. Listed in full
		// rather than derived from one representative position plus the centering, because
		// deriving it is a symmetry expansion this project has no local engine for (spglib lives
		// behind a Python subprocess) - and getting it wrong yields a plausible-looking, wrong
		// structure. Multiplicity is simply positions.size().
		std::vector<glm::vec3> positions;

		[[nodiscard]] int Multiplicity() const { return static_cast<int>(positions.size()); }
	};

	struct PrototypeDefinition
	{
		std::string name; // "diamond", "zincblende", "wurtzite", etc
		std::string description;
		std::string crystalSystem; // "Cubic", "Hexagonal", etc
		std::string centering; // "Primitive", "Body-centered", etc
		std::vector<SiteDefinition> sites;
		std::string bondLengthFormula; // "a * sqrt(3) / 4", "a / 2", etc
	};

	struct LatticeConstantSet
	{
		float a = 0.0f;
		float c = 0.0f; // for hexagonal/tetragonal
		float u = 0.0f; // for wurtzite
	};

	struct MaterialDefinition
	{
		std::string name; // "SiC", "GaAs", etc
		std::string description;
		std::map<std::string, std::string> polytypes; // "3C" -> "zincblende", "2H" -> "wurtzite", etc
		std::map<std::string, LatticeConstantSet> constants; // "exp", "PBE", "HSE06", "r2SCAN"
		std::map<std::string, std::vector<std::string>> defaultSiteMapping; // polytype -> [species for each site]
	};

	struct PrototypesAndMaterials
	{
		std::vector<PrototypeDefinition> prototypes;
		std::vector<MaterialDefinition> materials;
	};
}
