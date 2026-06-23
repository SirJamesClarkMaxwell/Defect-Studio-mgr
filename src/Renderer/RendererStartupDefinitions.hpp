#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct RendererStartupWindowDefinition
	{
		std::string title;
		std::string structureName;
		Path poscarPath;
		glm::vec3 direction = glm::vec3(1.0f, 1.0f, 1.0f);
		float distanceMultiplier = 1.0f;
		std::size_t hideStep = 0;
	};

	struct RendererStartupLayoutDefinition
	{
		std::vector<RendererStartupWindowDefinition> windows;
	};
} // namespace DefectStudio
