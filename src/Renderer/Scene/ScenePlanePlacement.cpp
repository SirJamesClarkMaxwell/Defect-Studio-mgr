#include "Core/dspch.hpp"
#include "Renderer/Scene/ScenePlanePlacement.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include "Renderer/Scene/SceneAxisAlignment.hpp"

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	bool SetScenePlaneInDefectFrame(RendererWindowState::ScenePlane &plane,
		const DefectFrame &frame, ScenePlaneDefectPreset preset)
	{
		if (!Finite(frame.origin) ||
			!SceneAxisAlignmentRotation(glm::mat3(1.0f), glm::mat3(frame.x, frame.y, frame.z), 0, 0))
			return false;
		glm::vec3 normal, tangent;
		switch (preset)
		{
		case ScenePlaneDefectPreset::XY: normal = frame.z; tangent = frame.x; break;
		case ScenePlaneDefectPreset::XZ: normal = frame.y; tangent = frame.x; break;
		case ScenePlaneDefectPreset::YZ: normal = frame.x; tangent = frame.y; break;
		default: return false;
		}
		plane.center = frame.origin;
		plane.normal = normal;
		plane.tangent = tangent;
		plane.anchorAtoms.clear();
		return true;
	}

	std::size_t PlaceSelectedScenePlanesInDefectFrame(RendererWindowState &window, ScenePlaneDefectPreset preset)
	{
		if (!window.structure.defectFrame) return 0;
		std::size_t count = 0;
		for (auto &plane : window.scenePlanes)
			if (std::find(window.selectedScenePlanes.begin(), window.selectedScenePlanes.end(), plane.id) != window.selectedScenePlanes.end())
				count += SetScenePlaneInDefectFrame(plane, *window.structure.defectFrame, preset) ? 1u : 0u;
		return count;
	}

	std::optional<glm::vec3> ScenePlaneEulerDegrees(const RendererWindowState::ScenePlane &plane)
	{
		const glm::mat3 basis(plane.tangent, glm::cross(plane.normal, plane.tangent), plane.normal);
		if (!SceneAxisAlignmentRotation(glm::mat3(1.0f), basis, 0, 0)) return std::nullopt;
		return glm::degrees(glm::eulerAngles(glm::normalize(glm::quat_cast(basis))));
	}

	bool SetScenePlaneEulerDegrees(RendererWindowState::ScenePlane &plane, const glm::vec3 &degrees)
	{
		if (!Finite(degrees) || !plane.anchorAtoms.empty()) return false;
		const glm::mat3 basis = glm::mat3_cast(glm::normalize(glm::quat(glm::radians(degrees))));
		plane.tangent = basis[0];
		plane.normal = basis[2];
		return true;
	}

	std::vector<std::size_t> ScenePlaneBackToFrontOrder(
		const std::vector<RendererWindowState::ScenePlane> &planes, const glm::vec3 &eye)
	{
		std::vector<std::size_t> order;
		if (!Finite(eye)) return order;
		for (std::size_t index = 0; index < planes.size(); ++index)
			if (planes[index].visible && Finite(planes[index].center)) order.push_back(index);
		const auto distanceSquared = [&](std::size_t index) {
			const glm::dvec3 offset = glm::dvec3(planes[index].center) - glm::dvec3(eye);
			return glm::dot(offset, offset);
		};
		std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
			return distanceSquared(a) > distanceSquared(b);
		});
		return order;
	}
}
