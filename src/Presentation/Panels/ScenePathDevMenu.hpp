#pragma once

#include <glm/glm.hpp>

#include "Renderer/Path/PathStyle.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	// Shared Line / Cubic / Arc creation presets.
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

	// Appends one path per PathDecorationKind except None, in enum order, each carrying that kind as
	// its start decoration and a plain Arrow at the end, parallel and far enough apart that
	// neighbouring tips do not touch. One undo entry for the whole gallery.
	//
	// task/41 S11j: this exists because reading the tip vocabulary off a screenshot is how the last
	// two rounds of shape defects were found, and rebuilding nine paths by hand to take that
	// screenshot is the kind of friction that stops it being done. Declared here rather than left in
	// the .cpp's anonymous namespace so a test can assert what it produced without driving ImGui.
	void AddScenePathDecorationGallery(RendererWindowState &windowState, const glm::vec3 &worldPosition);

}
