#pragma once

#include <string>
#include <vector>

#include <glm/vec2.hpp>

namespace DefectStudio
{
	// Where a docked panel sits relative to the dockspace's central node - the region the
	// VSCode-style Ctrl+B / Ctrl+J / Ctrl+Alt+I toggles operate on.
	//
	// Nothing in the app records dock placement: ImGuiLayer only ever calls
	// DockSpaceOverViewport, and the one existing query is DockBuilderGetCentralNode
	// (RendererPanel.cpp). So a region is derived from the live node rectangles every frame rather
	// than from a hardcoded panel list - a panel the user drags from the left edge to the right
	// edge changes region with it, which a static grouping could never follow.
	//
	// Top has no shortcut bound to it; it exists so a panel docked above the viewport is not
	// silently classified as Bottom.
	enum class DockRegion
	{
		Central,
		Left,
		Right,
		Bottom,
		Top,
		Floating,
	};

	struct DockRectangle
	{
		glm::vec2 min{};
		glm::vec2 max{};
	};

	// Classifies `node` by which side of `central` it falls entirely on:
	//   - a central rectangle with no area (no central node in the layout yet) -> Floating;
	//   - node entirely left of / right of / above / below central -> Left / Right / Top / Bottom;
	//   - a node clearing central on two axes at once (a corner dock) goes to whichever axis it
	//     clears by the larger distance, ties going to the horizontal one, because that is the
	//     axis the sidebar toggles own;
	//   - anything overlapping central on both axes -> Central.
	[[nodiscard]] DockRegion ClassifyDockRegion(const DockRectangle &node, const DockRectangle &central);

	// One region's hide/restore memory.
	//
	// Hiding records exactly which panels were visible; restoring shows those and nothing else, so
	// panels the user had already closed stay closed. The titles have to be remembered rather than
	// recomputed on restore: a hidden panel owns no ImGui window, so it has no dock node and
	// ClassifyDockRegion can no longer place it in this region at all.
	class DockRegionToggle
	{
	public:
		struct Decision
		{
			// Panel titles whose visibility must flip. Empty = nothing to do (an empty region with
			// nothing remembered).
			std::vector<std::string> titles;
			// true = show them (restoring), false = hide them.
			bool makeVisible = false;
		};

		// `visibleTitles` = titles of the panels currently visible in this region. Hides them if
		// any are visible, otherwise restores the set remembered by the last hide.
		[[nodiscard]] Decision Toggle(std::vector<std::string> visibleTitles);

		[[nodiscard]] bool IsHidden() const;

		// Drops the memory - for when the user reopens a remembered panel by hand, so a later
		// restore does not resurrect a set that no longer describes anything.
		void Forget();

	private:
		std::vector<std::string> m_HiddenTitles;
	};
} // namespace DefectStudio
