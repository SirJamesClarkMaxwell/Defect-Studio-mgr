#pragma once

namespace DefectStudio
{
	// The Scene Outliner's two per-row columns, Blender's split: the eye (drawn in the viewport,
	// what H toggles) and the camera (drawn in an exported render). The state logic lives in
	// Renderer/Scene/SceneVisibility.hpp; this is only the pair of widgets in front of a row label.
	//
	// ponytail: two checkboxes with tooltips rather than an eye and a camera glyph - the UI has no
	// icon font, and the toolbar's PNG icons are not reachable from a table cell this small.
	// Swap in real glyphs when an icon font lands.

	struct SceneVisibilityColumnState
	{
		bool visible = true;
		bool renderable = true;
		// Parent rows whose children disagree draw ImGui's mixed state instead of on or off.
		bool visibleMixed = false;
		bool renderableMixed = false;
	};

	struct SceneVisibilityColumnEdit
	{
		bool visibleChanged = false;
		bool renderableChanged = false;
		bool visible = true;
		bool renderable = true;
	};

	// Draws both toggles on the current line and leaves the cursor on the same line, ready for the
	// row's own label. A mixed column reports the value it was clicked to, so clicking a mixed
	// parent applies one value to every child.
	[[nodiscard]] SceneVisibilityColumnEdit DrawSceneVisibilityColumns(const SceneVisibilityColumnState &state);

	// Leaf-row convenience: writes through and reports whether anything changed.
	bool DrawSceneVisibilityColumns(bool &visible, bool &renderable);
} // namespace DefectStudio
