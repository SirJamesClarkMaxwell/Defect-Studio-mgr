#pragma once

#include <glm/glm.hpp>

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
	// style (Round tube, an Arrow end decoration). Pure - it allocates no SceneObjectId and touches no
	// window; AppendScenePath does that.
	[[nodiscard]] ScenePath MakeDevScenePath(ScenePathDevPreset preset, const glm::vec3 &worldPosition);

	// The viewport Add menu's "Path (dev)" submenu.
	//
	// Deliberately NOT the settings-backed switch the plan sketched: a flag on
	// RendererGlobalRenderSettings is not reachable from RendererPanel without new plumbing, and on
	// this branch nothing outside this submenu can reach a ScenePath anyway. The "(dev)" label and
	// this comment are the switch until S11 gives paths a real UI and S15 routes annotations through
	// them; whoever writes S11 deletes this file rather than growing it.
	//
	// Appends through SceneSystem::AppendScenePath and resyncs the registry mirror. No undo entry -
	// that is S8 - so a dev path survives Ctrl+Z only in the sense that undo does not know about it.
	void DrawScenePathDevAddMenu(RendererWindowState &windowState, const glm::vec3 &worldPosition);
}
