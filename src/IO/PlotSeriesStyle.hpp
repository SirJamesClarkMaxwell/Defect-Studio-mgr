#pragma once

#include <optional>
#include <glm/glm.hpp>

namespace DefectStudio
{
	// Persisted values only; ImGui/ImPlot interpretation belongs to Presentation.
	struct PlotSeriesStyle
	{
		std::optional<glm::vec4> color;
		float lineWeight = 1.5f;
		bool visible = true;
		bool filled = false;
		float fillAlpha = 0.25f;
		int lineStyle = 0; // solid, dashed, dotted
	};
}
