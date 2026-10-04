#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct AtomSite
	{
		std::string species;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		int index = 0;
		std::string label;
		float charge = 0.0f;
		float magnetization = 0.0f;
		float occupancy = 1.0f;
		std::array<bool, 3> selectiveDynamics = {true, true, true};
		bool hasSelectiveDynamics = false;
	};

	enum class BondOrigin
	{
		Auto,
		Manual
	};

	struct Bond
	{
		std::size_t firstAtomIndex = 0;
		std::size_t secondAtomIndex = 0;
		float lengthAngstrom = 0.0f;
		BondOrigin origin = BondOrigin::Auto;
		bool visible = true;
		// Integer count of lattice vectors (a, b, c) to add to secondAtomIndex's raw position to
		// get the correct minimum-image bond geometry - nonzero only for bonds that cross a
		// periodic cell boundary (e.g. a 2D sheet's edge atoms bonding to their image on the
		// opposite face). Zero for every non-periodic bond, so existing bonds are unaffected.
		glm::ivec3 periodicShift = glm::ivec3(0);
	};

	struct BondGenerationSettings
	{
		float globalCutoffScale = 1.18f;
		std::unordered_map<std::string, float> perPairCutoffOverride;
	};

	struct VacancySite
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		std::string sourceSpecies;
		std::string label;
		int index = 0;
		// Marker colour for this vacancy only (e.g. V_B vs V_N in hBN); empty = the shared vacancy
		// style. Display data, like `label`, kept here so it is saved and undone with the vacancy.
		std::optional<glm::vec3> color;

		[[nodiscard]] std::string GetLabel() const
		{
			if (!label.empty())
				return label;
			if (sourceSpecies.empty())
				return "V";
			return "V_" + sourceSpecies;
		}
	};

	// Local axes of a defect: an origin and a right-handed orthonormal triad, e.g. z along the N-V
	// axis of NV-. Stored as positions, not atom bindings, so it does not follow a relaxation.
	struct DefectFrame
	{
		glm::vec3 origin = glm::vec3(0.0f);
		glm::vec3 x = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 y = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 z = glm::vec3(0.0f, 0.0f, 1.0f);
	};

	struct LatticeCell
	{
		std::array<glm::vec3, 3> vectors = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)};

		[[nodiscard]] glm::mat3 ToMatrix() const;
		[[nodiscard]] glm::mat3 ToInverseMatrix() const;
	};
} // namespace DefectStudio
