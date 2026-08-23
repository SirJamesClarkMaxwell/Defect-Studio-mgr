#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Utils/Memory.hpp"

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

	// Copies of this class share their underlying data (Ref-held, shared_ptr semantics) rather than
	// each holding an independent snapshot - deliberate, not an accident of using Ref for
	// convenience: this table is copied by value into every renderer command at registration time
	// (RendererCommandRegistration.cpp) and into every open window's rebuild path, all long before
	// any UI could exist to edit it. Without sharing, ElementCatalogPanel editing the table found in
	// Application::m_RendererAtomStyleTable would never be visible to those already-bound copies -
	// the edit would need a way to reach every one of them individually, and there is no such
	// mechanism (commands aren't reconstructed after registration). ReplaceStyles/Clear mutate the
	// shared map in place for exactly this reason - never replace m_Styles/m_VacancyStyle wholesale.
	class AtomStyleTable
	{
	public:
		AtomStyleTable()
			: m_Styles(CreateRef<std::unordered_map<std::string, AtomRenderStyle>>()),
			  m_FallbackStyle(CreateRef<AtomRenderStyle>()),
			  m_VacancyStyle(CreateRef<VacancyRenderStyle>())
		{
		}

		void ReplaceStyles(
			std::unordered_map<std::string, AtomRenderStyle> styles,
			VacancyRenderStyle vacancyStyle);
		void Clear();

		[[nodiscard]] const AtomRenderStyle &GetStyle(const std::string &symbol) const;
		[[nodiscard]] const VacancyRenderStyle &GetVacancyStyle() const;

		[[nodiscard]] glm::vec3 Color(const std::string &symbol) const;
		[[nodiscard]] float DisplayRadius(const std::string &symbol) const;
		[[nodiscard]] std::size_t Size() const;

		// Snapshot for iteration (the Element Catalog editor needs to list every entry) - a real
		// copy of the map, not a reference into the shared one, so the caller can safely edit rows
		// without mutating live render state until it explicitly calls ReplaceStyles.
		[[nodiscard]] std::unordered_map<std::string, AtomRenderStyle> AllStyles() const
		{
			return *m_Styles;
		}

	private:
		Ref<std::unordered_map<std::string, AtomRenderStyle>> m_Styles;
		Ref<AtomRenderStyle> m_FallbackStyle;
		Ref<VacancyRenderStyle> m_VacancyStyle;
	};
} // namespace DefectStudio
