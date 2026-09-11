#pragma once

#include <imgui.h>

namespace DefectStudio
{
	class RendererLayer;
	struct RendererWindowState;

	// Ray-cast atom pick for one viewport, publishing AtomSelectionRequested (a miss publishes an
	// empty selection, which is how a click on background clears it). Free functions, not
	// RendererPanel members, for the same reason ViewportInput's are: the three-pane creation window
	// draws its own viewports and would otherwise have no way to select anything at all.
	void HandleAtomPick(
		RendererWindowState &windowState, float relX, float relY, bool additive, RendererLayer &layer);

	// Plain-click entry point for a viewport (unlike HandleAtomPick, which stays atom-only for the
	// measure tool's pair/triple picking) - tries atoms first, then bonds, so an atom under the
	// cursor always wins a bond behind/near it.
	void HandleViewportPick(
		RendererWindowState &windowState, float relX, float relY, bool additive, RendererLayer &layer);
} // namespace DefectStudio
