#pragma once

#include <cmath>
#include <numbers>

#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio
{
	// Shared by drawing and picking. Vertices are in structure space, before sceneOffset.
	[[nodiscard]] inline std::vector<glm::vec3> BuildHideVolumeSilhouette(
		const SceneHideVolume &volume, const RendererStructureData &structure,
		const RendererViewCamera &camera, const glm::vec3 &sceneOffset)
	{
		if (volume.kind != HideVolumeKind::Sphere || volume.frame != HideVolumeFrame::Anchored)
			return {};
		float radius = volume.halfExtents.x;
		if (!std::isfinite(radius) || radius <= 0.0f) return {};
		glm::vec3 center = HideVolumeWorldCenter(volume, structure);
		const glm::mat4 view = camera.ViewMatrix();
		glm::vec3 normal(view[0][2], view[1][2], view[2][2]);
		if (camera.Projection() == CameraProjection::Perspective)
		{
			const glm::vec3 toCamera = camera.Position() - center - sceneOffset;
			const float distance = glm::length(toCamera);
			if (!std::isfinite(distance) || distance <= radius) return {};
			normal = toCamera / distance;
			// At tangency (p-c) dot (camera-p) = 0, hence (p-c) dot normal = r^2/d.
			const float ratio = radius / distance;
			center += radius * ratio * normal;
			radius *= std::sqrt(1.0f - ratio * ratio);
		}
		for (int axis = 0; axis < 3; ++axis)
			if (!std::isfinite(center[axis]) || !std::isfinite(normal[axis])) return {};
		const glm::vec3 reference = std::abs(normal.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		const glm::vec3 tangent = glm::normalize(glm::cross(reference, normal));
		const glm::vec3 bitangent = glm::cross(normal, tangent);
		constexpr int segments = 96;
		const auto point = [&](int index) {
			const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / segments;
			return center + radius * (std::cos(angle) * tangent + std::sin(angle) * bitangent);
		};
		std::vector<glm::vec3> edges;
		edges.reserve(segments * 2);
		for (int index = 0; index < segments; ++index)
		{
			edges.push_back(point(index));
			edges.push_back(point((index + 1) % segments));
		}
		return edges;
	}
}
