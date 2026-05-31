#pragma once

#include <string>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct VacancySite
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		std::string sourceSpecies;
		std::string label;
		int index = 0;

		[[nodiscard]] std::string GetLabel() const
		{
			if (!label.empty())
				return label;
			if (sourceSpecies.empty())
				return "V";
			return "V_" + sourceSpecies;
		}
	};
} // namespace DefectStudio
