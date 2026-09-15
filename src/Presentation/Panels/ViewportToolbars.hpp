#pragma once

namespace DefectStudio
{
	class RendererLayer;
	class RendererViewCamera;
	struct RendererWindowState;
	// Toolbar sizes were tuned at this shipped text scale before they became proportional.
	inline constexpr float kViewportToolbarFontScaleBaseline = 2.34f;

	// The horizontal and vertical viewport toolbars, drawn identically by every viewport - the
	// ordinary renderer windows (RendererPanel) and the three-pane structure-creation window
	// (StructureCreationTabsPanel), which draws ONE pair of toolbars acting on its active pane.
	// Free functions rather than RendererPanel members precisely so the second caller exists:
	// they only ever needed a window state and the layer.
	void DrawViewportToolbar(RendererWindowState &windowState, RendererLayer &layer);
	void DrawViewportVerticalToolbar(RendererWindowState &windowState, RendererLayer &layer);
	void DrawViewportTransformControls(RendererWindowState &windowState, RendererLayer &layer, float uiScale);
	// Frames every atom from the default (1, 1, 0.9) direction - toolbar and navigation gizmo "home".
	[[nodiscard]] RendererViewCamera ComputeResetViewCamera(const RendererWindowState &windowState);
} // namespace DefectStudio
