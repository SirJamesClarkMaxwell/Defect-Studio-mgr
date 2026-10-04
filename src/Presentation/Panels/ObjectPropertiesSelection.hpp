#pragma once

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	struct ObjectPropertiesSections
	{
		bool atoms = false;
		bool labels = false;
		bool arrows = false;
		bool orbitals = false;
		bool planes = false;
		bool paths = false;
		bool vacancies = false;
		bool defectFrame = false;

		[[nodiscard]] bool Empty() const;
	};

	// Keeps the ImGui panel's routing testable without ImGui: each flag says whether the current
	// selection needs that kind's selected-object editor. Full scene-wide lists are a separate,
	// opt-in "Wszystkie obiekty" view and do not affect these flags.
	[[nodiscard]] ObjectPropertiesSections ResolveObjectPropertiesSections(
		const RendererWindowState &windowState);
} // namespace DefectStudio
