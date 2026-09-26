#pragma once

#include <glm/glm.hpp>

#include "Renderer/Path/PathStyle.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	// Which demo path the dev submenu appends. Not a user-facing concept: it exists so S7 can put a
	// Line, a Cubic and an Arc on screen for the manual round without waiting for the real commands
	// (S8) or the real UI (S11).
	enum class ScenePathDevPreset
	{
		Line,
		Cubic,
		Arc,
	};

	// One ScenePath centred on `worldPosition`, roughly two Angstrom across, with a visible default
	// style (an Arrow end decoration). Pure - it allocates no SceneObjectId and touches no window;
	// AddScenePath does that.
	//
	// `profile` is the S11 creation-preset axis: Round is the tube, Flat the ribbon in the
	// transported frame, CameraFacing the ribbon turned towards the eye. It is defaulted so every
	// pre-S11 call keeps producing exactly the path it produced before. A CameraFacing or Flat
	// preset is made a little wider than the tube, because a ribbon seen edge-on at the tube's
	// width is a line one pixel thick and reads as a failed add.
	[[nodiscard]] ScenePath MakeDevScenePath(
		ScenePathDevPreset preset, const glm::vec3 &worldPosition, StrokeProfile profile = StrokeProfile::Round);

	// The viewport Add menu's "Path (dev)" submenu.
	//
	// Deliberately NOT the settings-backed switch the plan sketched: a flag on
	// RendererGlobalRenderSettings is not reachable from RendererPanel without new plumbing, and on
	// this branch nothing outside this submenu can reach a ScenePath anyway. The "(dev)" label and
	// this comment are the switch until S11 gives paths a real UI and S15 routes annotations through
	// them; whoever writes S11 deletes this file rather than growing it.
	//
	// Appends through AddScenePath with the renderer's own undo sink, so a dev path is one Ctrl+Z
	// away exactly like an arrow or a plane - S8 made that the only way a path edit reaches the store.
	void DrawScenePathDevAddMenu(RendererWindowState &windowState, const glm::vec3 &worldPosition);
}
