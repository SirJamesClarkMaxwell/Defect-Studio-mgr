#pragma once

#include <string>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct AtomSite
	{
		std::string species;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		int index = 0;
	};
} // namespace DefectStudio
