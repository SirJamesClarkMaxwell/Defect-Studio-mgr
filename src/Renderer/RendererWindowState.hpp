#pragma once

#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererViewCamera.hpp"

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/Scene/SceneRegistry.hpp"

namespace DefectStudio
{
	struct RendererToolbarIconTexture
	{
		unsigned int rendererId = 0;
		int width = 0;
		int height = 0;
		bool loadAttempted = false;
	};

	struct RendererWindowState
	{
		std::string windowId;
		std::string title;
		RendererStructureData structure;
		// One entity per atom/bond, synced with `structure` by SceneSystem::SyncSceneWithStructure
		// (called on load/reload). Owns SelectionComponent/VisibilityComponent - the flat arrays
		// above (structure.atoms[i].visible, selectedAtomIndices below) stay the GPU-instanced
		// rendering hot path and are kept as a mirror via SceneSystem::PushSelectionAndVisibilityToWindowState.
		SceneRegistry sceneRegistry;
		Unique<RendererViewCamera> camera;
		glm::vec2 viewportSize = glm::vec2(640.0f, 480.0f);
		bool showGrid = true;
		bool showCellBox = true;
		bool showBonds = true;
		bool showAtoms = true;
		std::vector<std::size_t> selectedAtomIndices;
		float rotationStepDeg = 1.0f;
		float pixelStepPx = 10.0f;
		float percentStep = 10.0f;
		bool dragActive = false;
		bool lastFocusedState = false;
		bool transitionActive = false;
		float transitionElapsed = 0.0f;
		float transitionDuration = 0.14f;
		glm::vec3 transitionStartTarget = glm::vec3(0.0f);
		glm::vec3 transitionEndTarget = glm::vec3(0.0f);
		float transitionStartDistance = 0.0f;
		float transitionEndDistance = 0.0f;
		float transitionStartYaw = 0.0f;
		float transitionEndYaw = 0.0f;
		float transitionStartPitch = 0.0f;
		float transitionEndPitch = 0.0f;
		float transitionStartRoll = 0.0f;
		float transitionEndRoll = 0.0f;
		glm::quat transitionStartOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::quat transitionEndOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		std::string transitionSourceAction;
		std::vector<RendererViewStateChange> viewUndoHistory;
		std::vector<RendererViewStateChange> viewRedoHistory;
		bool viewInteractionActive = false;
		std::string viewInteractionSource;
		RendererViewSnapshot viewInteractionStart;
		// Box/circle drag-select (Alt+B / Alt+C). Coordinates are viewport-relative pixels, same
		// space as RendererPanel::handleAtomPick's relX/relY.
		SelectionToolMode activeSelectionTool = SelectionToolMode::None;
		bool selectionDragActive = false;
		glm::vec2 selectionDragStart = glm::vec2(0.0f);
		glm::vec2 selectionDragCurrent = glm::vec2(0.0f);
		// Circle-select brush radius in viewport pixels - persistent per window, adjusted with the
		// mouse wheel while the circle tool is active (scroll up = bigger, down = smaller).
		float circleSelectRadius = 48.0f;
		// GPU compute-shader isosurface mesh for one spin channel's rendered orbital
		// (ElectronicStructurePanel, via RendererLayer::RegenerateOrbitalIsosurface). Two
		// independent channels so spin-up and spin-down can be shown simultaneously - `enabled`
		// gates drawing (data/vertexCount can stay populated while temporarily hidden).
		struct OrbitalOverlayChannel
		{
			bool enabled = false;
			int vertexCount = 0;
			// Positive = red, negative = blue - same convention for both spin channels.
			glm::vec3 positiveLobeColor = glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 negativeLobeColor = glm::vec3(0.0f, 0.0f, 1.0f);
			float lobeAlpha = 0.6f;
		};
		OrbitalOverlayChannel orbitalChannelUp;
		OrbitalOverlayChannel orbitalChannelDown;

		// One-shot: on this window's very first Begin(), RendererPanel docks it into the
		// dockspace's central node (ImGuiCond_FirstUseEver) instead of opening free-floating -
		// never reapplied afterwards, so a later manual re-dock by the user sticks.
		bool dockingInitialized = false;
	};

	// T15-lite export dialog: resolution preset + filename proposed from the structure's source
	// path + pan-to-reframe preview, rendered at the target aspect ratio so the exported image is
	// never stretched (aspect ratio is correct by construction - "crop" here means reframing via
	// pan, not a post-render pixel crop). One dialog instance application-wide (not per-window).
	struct RenderExportDialogState
	{
		enum class ResolutionPreset
		{
			FullHd1080p,
			QuadHd2K,
			UltraHd4K,
			Custom
		};

		bool open = false;
		std::string targetWindowId;
		std::string filename;
		Path saveDirectory = Path("exports");
		ResolutionPreset preset = ResolutionPreset::FullHd1080p;
		int customWidth = 1920;
		int customHeight = 1080;
		// Fractions (0..1) trimmed from each edge on export - a real pixel crop (changes the
		// output aspect ratio), independent of the pan/zoom reframing above.
		float cropLeft = 0.0f;
		float cropRight = 0.0f;
		float cropTop = 0.0f;
		float cropBottom = 0.0f;
		// Owns its own camera (copied from the target window's live camera when the dialog opens,
		// then mutated in place each frame by the preset/pan controls) - kept separate from the
		// canonical RendererWindowState list, never touches the live window's own camera/state.
		RendererWindowState previewState;
	};
} // namespace DefectStudio
