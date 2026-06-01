#include "Core/dspch.hpp"

#include "Domain/Crystal/CrystalPrimitives.hpp"

#include <cmath>

#include <glm/gtc/matrix_inverse.hpp>

namespace DefectStudio
{
	glm::mat3 LatticeCell::ToMatrix() const
	{
		glm::mat3 matrix(1.0f);
		matrix[0] = vectors[0];
		matrix[1] = vectors[1];
		matrix[2] = vectors[2];
		return matrix;
	}

	glm::mat3 LatticeCell::ToInverseMatrix() const
	{
		const glm::mat3 matrix = ToMatrix();
		const float determinant = glm::determinant(matrix);
		if (std::abs(determinant) <= 1e-8f)
			return glm::mat3(1.0f);
		return glm::inverse(matrix);
	}
} // namespace DefectStudio
