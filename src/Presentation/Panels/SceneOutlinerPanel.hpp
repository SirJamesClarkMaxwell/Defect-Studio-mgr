#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	// Lists open renderer windows (one structure per window - no Collections, see docs/work/
	// project/plans/2026-08-23-outliner-bonds-displacement.md). Per-window visibility toggle
	// (show/hide every atom+bond in that window, same VisibilityComponent mechanism as H/Alt+H) and
	// name rename (double-click, or F2 on the last-clicked row - writes RendererWindowState::title,
	// which already drives the docked viewport window's own tab label). Expanding a window's tree
	// arrow lists its atoms grouped by species (a flat per-atom list doesn't scale to a supercell's
	// atom count, but its handful of distinct elements do) - a species row's own checkbox/arrow
	// mirrors the window row's, and an atom row click selects it in the viewport (Ctrl+click is
	// additive, matching RendererPanel::handleViewportPick).
	class SceneOutlinerPanel final : public IPanel
	{
	public:
		explicit SceneOutlinerPanel(RendererLayer &layer, std::string title = "Scene Outliner", bool visibleByDefault = false);
		SceneOutlinerPanel(const SceneOutlinerPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void drawSpeciesGroup(RendererWindowState &windowState, const std::string &species, const std::vector<std::size_t> &atomIndices);
		void drawAtomRow(RendererWindowState &windowState, std::size_t atomIndex);

		RendererLayer &m_Layer;
		// Index into m_Layer.GetWindows(), not a stable windowId - fine since both editing state and
		// active-row tracking are cleared the moment the window list changes shape (see Render()).
		int m_EditingWindowIndex = -1;
		int m_ActiveWindowIndex = -1;
		bool m_JustStartedEditing = false;
		std::array<char, 128> m_EditingBuffer{};
	};
} // namespace DefectStudio
