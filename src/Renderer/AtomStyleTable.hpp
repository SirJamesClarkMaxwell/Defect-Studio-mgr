#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct AtomRenderStyle
	{
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
		float displayRadius = 0.40f;
	};

	enum class VacancyRenderMode
	{
		Ghost,
		Wireframe,
		Solid,
	};

	struct VacancyRenderStyle
	{
		glm::vec3 color = glm::vec3(0.72f, 0.20f, 0.82f);
		float displayRadius = 0.45f;
		float opacity = 0.35f;
		VacancyRenderMode renderMode = VacancyRenderMode::Ghost;
	};

	class AtomStyleTable
	{
	public:
		void ReplaceStyles(
			std::unordered_map<std::string, AtomRenderStyle> styles,
			VacancyRenderStyle vacancyStyle);
		void Clear();

		[[nodiscard]] const AtomRenderStyle &GetStyle(const std::string &symbol) const;
		[[nodiscard]] const VacancyRenderStyle &GetVacancyStyle() const;

		[[nodiscard]] glm::vec3 Color(const std::string &symbol) const;
		[[nodiscard]] float DisplayRadius(const std::string &symbol) const;
		[[nodiscard]] std::size_t Size() const;

	private:
		std::unordered_map<std::string, AtomRenderStyle> m_Styles;
		AtomRenderStyle m_FallbackStyle;
		VacancyRenderStyle m_VacancyStyle;
	};
} // namespace DefectStudio
