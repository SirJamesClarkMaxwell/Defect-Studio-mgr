#pragma once

#include "Renderer/Scene/ScenePlaneGeometry.hpp"

namespace DefectStudio
{
	enum class ScenePlaneDefectPreset { XY, XZ, YZ };

	// Keeps size/style/id, detaches atom anchors. Invalid frames leave the plane untouched.
	bool SetScenePlaneInDefectFrame(RendererWindowState::ScenePlane &plane,
		const DefectFrame &frame, ScenePlaneDefectPreset preset);
	std::size_t PlaceSelectedScenePlanesInDefectFrame(RendererWindowState &window,
		ScenePlaneDefectPreset preset);

	// Same XYZ degrees/quaternion convention as scene orbitals and paths.
	[[nodiscard]] std::optional<glm::vec3> ScenePlaneEulerDegrees(const RendererWindowState::ScenePlane &plane);
	bool SetScenePlaneEulerDegrees(RendererWindowState::ScenePlane &plane, const glm::vec3 &degrees);

	// Visible finite centres only; equal distances retain scene order. Eye is in scene coordinates.
	[[nodiscard]] std::vector<std::size_t> ScenePlaneBackToFrontOrder(
		const std::vector<RendererWindowState::ScenePlane> &planes, const glm::vec3 &eye);
}
