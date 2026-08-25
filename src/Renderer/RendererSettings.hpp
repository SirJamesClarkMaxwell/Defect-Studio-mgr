#pragma once

#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

namespace DefectStudio
{
	enum class CameraProjection
	{
		Perspective,
		Orthographic
	};

	struct RendererLightingSettings
	{
		glm::vec3 keyDirection = glm::normalize(glm::vec3(0.6f, 0.8f, 0.5f));
		glm::vec3 fillDirection = glm::normalize(glm::vec3(-0.7f, 0.3f, 0.2f));
		glm::vec3 backDirection = glm::normalize(glm::vec3(0.0f, -0.4f, -0.8f));
		float ambientIntensity = 0.18f;
		float keyIntensity = 0.55f;
		float fillIntensity = 0.25f;
		float backIntensity = 0.12f;
		bool twoSided = true;
		// Blinn-Phong specular from the key light only (fill/back stay pure diffuse - a second and
		// third highlight would just look noisy on small spheres). Applies to atoms, bonds, and
		// isosurfaces.
		float specularIntensity = 0.35f;
		float shininess = 72.0f;
		// Fresnel rim glow, isosurface only - brightens a lobe's silhouette edge at grazing view
		// angles, the common "misty" look for electron-density isosurfaces (rim tinted by the lobe's
		// own color, not a fixed color).
		float rimIntensity = 0.35f;
		float rimPower = 2.5f;
	};

	struct RendererViewportSettings
	{
		float axisButtonSize = 20.0f;
		float iconButtonSize = 25.0f;
	};

	struct RendererToolbarWheelSettings
	{
		float rotationStepDelta = 1.0f;
		float zoomStepDelta = 1.0f;
		std::vector<float> ctrlPresetValues = {
			0.0f,
			1.0f,
			3.0f,
			5.0f,
			10.0f,
			15.0f,
			30.0f,
			45.0f,
			60.0f,
			90.0f,
			180.0f};
	};

	struct RendererGridSettings
	{
		bool autoFitToStructureBounds = true;
		float paddingPercent = 20.0f;
		float spacing = 0.7f;
		float planeZ = 0.0f;
	};

	struct RendererGlobalRenderSettings
	{
		glm::vec4 backgroundColor = glm::vec4(0.06f, 0.07f, 0.08f, 1.0f);
		// Uniform scale on every bond cylinder's radius, applied in bonds.vert (radial local axes
		// only, so orientation/length/normals stay correct) - live like the lighting sliders below,
		// no cache invalidation needed.
		float bondRadiusMultiplier = 1.0f;
		// Luma-preserving saturation multiplier (1 = unchanged, 0 = grayscale, >1 = boosted) applied
		// in atoms/bonds/isosurface fragment shaders to the already-lit color, right before the
		// final clamp - fixes the "washed out / pastel" look some users see on a light background by
		// letting them punch color back in without touching per-element AtomStyleTable colors.
		float colorSaturation = 1.0f;
		// Interactive-viewport supersample factor - the FBO is rendered at viewportSize * this, then
		// ImGui::Image displays it back down at the panel's native size, same idea as SSAA. 1 = off
		// (native resolution, default). Export has its own separate resolution presets
		// (RenderExportDialogState) - this only affects the live/interactive view.
		float viewportSupersample = 1.0f;
		float orbitSensitivity = 1.0f;
		float panSensitivity = 1.0f;
		float zoomSensitivity = 1.0f;
		float rotationSpeed = 1.0f;
		float focusSelectedAtomDistance = 3.0f;
		float focusSelectedAtomTransitionSeconds = 0.18f;
		bool focusSelectedAtomRespectAtomRadius = true;
		float focusSelectedAtomRadiusMultiplier = 2.0f;
		bool invertZoom = false;
		bool touchpadNavigation = true;
		CameraProjection defaultCameraProjection = CameraProjection::Perspective;
		bool autoApplyDefaultViewOnOpen = false;
		RendererLightingSettings lighting;
		RendererViewportSettings viewport;
		RendererToolbarWheelSettings toolbarWheel;
		RendererGridSettings grid;
	};
} // namespace DefectStudio
