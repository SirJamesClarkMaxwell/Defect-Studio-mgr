#include "Core/dspch.hpp"

#include "Domain/Crystal/PrimitiveCell.hpp"

#include <string>

namespace DefectStudio
{
	std::optional<glm::mat3> PrimitiveCellVectors(
		const glm::mat3 &conventionalLattice,
		BravaisCenteringPreset centering)
	{
		// Rows are the primitive vectors expressed in the conventional ones - the standard
		// transformation matrices (see any crystallography text's table of centring types). The
		// determinant of each is 1/(number of lattice points in the conventional cell): 1/2 for I
		// and C, 1/4 for F.
		glm::mat3 transform(1.0f);
		switch (centering)
		{
			case BravaisCenteringPreset::Primitive:
				return std::nullopt;
			case BravaisCenteringPreset::BodyCentered:
				transform = glm::mat3(
					glm::vec3(-0.5f, 0.5f, 0.5f),
					glm::vec3(0.5f, -0.5f, 0.5f),
					glm::vec3(0.5f, 0.5f, -0.5f));
				break;
			case BravaisCenteringPreset::FaceCentered:
				transform = glm::mat3(
					glm::vec3(0.0f, 0.5f, 0.5f),
					glm::vec3(0.5f, 0.0f, 0.5f),
					glm::vec3(0.5f, 0.5f, 0.0f));
				break;
			case BravaisCenteringPreset::BaseCentered:
				transform = glm::mat3(
					glm::vec3(0.5f, 0.5f, 0.0f),
					glm::vec3(-0.5f, 0.5f, 0.0f),
					glm::vec3(0.0f, 0.0f, 1.0f));
				break;
		}

		glm::mat3 primitive(1.0f);
		for (int row = 0; row < 3; ++row)
		{
			primitive[row] = transform[row].x * conventionalLattice[0] +
				transform[row].y * conventionalLattice[1] +
				transform[row].z * conventionalLattice[2];
		}
		return primitive;
	}

	std::optional<BravaisCenteringPreset> ParseCenteringName(const std::string &name)
	{
		if (name == "Primitive")
			return BravaisCenteringPreset::Primitive;
		if (name == "Body-centered" || name == "Body-centred")
			return BravaisCenteringPreset::BodyCentered;
		if (name == "Face-centered" || name == "Face-centred")
			return BravaisCenteringPreset::FaceCentered;
		if (name == "Base-centered" || name == "Base-centred")
			return BravaisCenteringPreset::BaseCentered;
		return std::nullopt;
	}
} // namespace DefectStudio
