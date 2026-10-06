#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	class AtomStyleTable;
	class CommandRegistry;
	class DomainLayer;
	class RendererLayer;
	struct RendererWindowState;

	// Vacancy markers as selectable things. RendererWindowState::selectedVacancies holds indices into
	// structure.vacancies (= the domain list, same order); every edit still goes through
	// renderer.vacancy.set, so undo and the scene_objects.yaml sidecar work as before.

	// Nearest shown marker whose (padded) disc sphere the world ray enters.
	[[nodiscard]] std::optional<std::size_t> PickVacancyAlongRay(
		const RendererWindowState &windowState, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection);

	// Click-select in the viewport interaction chain: plain click replaces (and clears atoms and
	// every scene-object kind), Ctrl-click toggles. Picks when atoms or labels are pickable.
	[[nodiscard]] bool HandleVacancyInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);

	// Delete removes the selected vacancies in one undo step. Unconditional per frame, like the pin
	// keyboard shortcuts.
	void HandleVacancyKeyboardShortcuts(RendererWindowState &windowState, bool hovered, RendererLayer &layer,
		DomainLayer *domain, CommandRegistry *registry);

	// Selection ring around each selected marker (ImGui overlay).
	void DrawSelectedVacancyOverlay(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize);

	// Object Properties section: label (domain, undoable), position, delete, and the vacancy style.
	// The style is shared by every vacancy (AtomStyleTable::GetVacancyStyle); null `styles` shows a
	// pointer to the Element Catalog instead of the editor.
	void DrawSelectedVacancySection(RendererWindowState &windowState, RendererLayer &layer, DomainLayer *domain,
		CommandRegistry *registry, AtomStyleTable *styles);

	// The shared vacancy style editor (Element Catalog and the section above). Writes the table and
	// the baked copies in every open window; returns true when something changed.
	bool DrawVacancyStyleEditor(AtomStyleTable &styles, RendererLayer &layer);
} // namespace DefectStudio
