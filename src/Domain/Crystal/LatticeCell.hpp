#pragma once

#include <array>

#include <glm/glm.hpp>

namespace DefectStudio
{
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
