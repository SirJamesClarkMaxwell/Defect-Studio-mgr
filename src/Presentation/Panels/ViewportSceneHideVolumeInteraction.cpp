#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneHideVolume.hpp"
#include "Renderer/Scene/SceneSelection.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		bool ClipSegment(glm::vec4 &a, glm::vec4 &b)
		{
			// Clip before dividing by w: a segment crossing the near plane still has a visible part.
			const float first[] = {a.w + a.x, a.w - a.x, a.w + a.y, a.w - a.y, a.w + a.z, a.w - a.z};
			const float second[] = {b.w + b.x, b.w - b.x, b.w + b.y, b.w - b.y, b.w + b.z, b.w - b.z};
			float enter = 0, leave = 1;
			for (int plane = 0; plane < 6; ++plane)
			{
				const float da = first[plane], db = second[plane];
				if (!std::isfinite(da) || !std::isfinite(db) || (da < 0 && db < 0)) return false;
				if ((da < 0) != (db < 0))
				{
					const float t = da / (da - db);
					if (da < 0) enter = std::max(enter, t);
					else leave = std::min(leave, t);
				}
			}
			if (enter > leave) return false;
			const glm::vec4 delta = b - a;
			b = a + delta * leave;
			a += delta * enter;
			return a.w > 1e-6f && b.w > 1e-6f;
		}
	}

	std::optional<SceneObjectId> PickSceneHideVolumeOutline(
		const RendererWindowState &window, glm::vec2 mouse, glm::vec2 imageSize)
	{
		if (!window.pickLabels || !window.camera || imageSize.x <= 0 || imageSize.y <= 0 ||
			!SelectionHitTest::PointInRect(mouse, glm::vec2(0), imageSize)) return std::nullopt;
		const auto ray = ComputeViewportPickRay(window, mouse, imageSize);
		if (!ray) return std::nullopt;
		const glm::vec3 origin = ray->origin;
		if ((window.pickAtoms && PickAtomAlongRay(window, origin, ray->direction)) ||
			(window.pickBonds && PickBondAlongRay(window, origin, ray->direction))) return std::nullopt;

		const auto viewProjection = window.camera->ProjectionMatrix() * window.camera->ViewMatrix();
		std::optional<SceneObjectId> hit;
		float bestDistance = 6.0f, bestDepth = std::numeric_limits<float>::max();
		const auto screen = [imageSize](glm::vec4 clip) {
			return glm::vec2((clip.x / clip.w * 0.5f + 0.5f) * imageSize.x,
				(0.5f - clip.y / clip.w * 0.5f) * imageSize.y);
		};
		for (const auto &volume : window.sceneHideVolumes)
		{
			if (!volume.visible) continue;
			const auto edges = BuildHideVolumeEdges(volume, window.structure);
			for (std::size_t segment = 0; segment + 1 < edges.size(); segment += 2)
			{
				auto a = viewProjection * glm::vec4(edges[segment] + window.viewOffset, 1);
				auto b = viewProjection * glm::vec4(edges[segment + 1] + window.viewOffset, 1);
				if (!ClipSegment(a, b)) continue;
				const glm::vec2 start = screen(a), end = screen(b), delta = end - start;
				const float distance = SelectionHitTest::DistancePointToSegment(mouse, start, end);
				const float lengthSquared = glm::dot(delta, delta);
				const float t = lengthSquared > 1e-6f ? std::clamp(glm::dot(mouse - start, delta) / lengthSquared, 0.0f, 1.0f) : 0;
				const float depth = (1 - t) * a.z / a.w + t * b.z / b.w;
				if (distance <= 6.0f && (distance < bestDistance - 1e-4f ||
					(std::abs(distance - bestDistance) <= 1e-4f && depth < bestDepth)))
				{
					bestDistance = distance;
					bestDepth = depth;
					hit = volume.id;
				}
			}
		}
		return hit;
	}

	bool HandleSceneHideVolumeInteraction(
		RendererWindowState &window, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
		const ImVec2 mouse = ImGui::GetMousePos();
		const auto hit = PickSceneHideVolumeOutline(window,
			glm::vec2(mouse.x - imageOrigin.x, mouse.y - imageOrigin.y), glm::vec2(imageSize.x, imageSize.y));
		if (!hit) return false;
		const auto &io = ImGui::GetIO();
		if (!io.KeyCtrl && !io.KeyShift)
		{
			ClearAllSceneSelection(window);
			window.selectedSceneHideVolumes = {*hit};
		}
		else
		{
			auto &selection = window.selectedSceneHideVolumes;
			const auto found = std::find(selection.begin(), selection.end(), *hit);
			if (found == selection.end()) selection.push_back(*hit);
			else if (io.KeyCtrl) selection.erase(found);
		}
		SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
		return true;
	}
}
