#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio
{
	glm::vec3 ApplySceneSelectionHighlight(const glm::vec3 &base, const bool selected, const float strength)
	{
		return selected ? glm::mix(base, SceneSelectionHighlightColor(), strength) : base;
	}

} // namespace DefectStudio
