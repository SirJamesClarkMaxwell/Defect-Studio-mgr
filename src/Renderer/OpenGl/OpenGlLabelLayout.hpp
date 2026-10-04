#pragma once

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/Text/TexMarkup.hpp"

namespace DefectStudio
{
	struct LabelLocalBounds
	{
		glm::vec2 min = glm::vec2(0.0f);
		glm::vec2 max = glm::vec2(0.0f);
		bool hasBounds = false;
	};

	LabelLocalBounds AppendLabelInstances(const MsdfFont &font, const glm::vec3 &worldCenter,
		const std::vector<TexGlyph> &text, std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style, float rotationRadians, bool selected);
	void AppendLabelBackgroundInstance(const glm::vec3 &worldCenter, const LabelLocalBounds &bounds,
		const RendererWindowState::LabelStyle &style, float rotationRadians,
		std::vector<OpenGlLabelInstance> &outInstances, bool selected = false);
	LabelLocalBounds AppendBondLabelInstances(const MsdfFont &font, const glm::vec3 &midpoint,
		float lengthAngstrom, std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style = {}, float rotationRadians = 0.0f, bool selected = false);
	LabelLocalBounds AppendAngleLabelInstances(const MsdfFont &font, const glm::vec3 &vertex,
		float angleDeg, std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style = {}, float rotationRadians = 0.0f, bool selected = false);
} // namespace DefectStudio
