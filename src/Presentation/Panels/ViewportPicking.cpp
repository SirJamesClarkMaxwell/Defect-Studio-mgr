#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportPicking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		// A plain (non-additive) click on an atom, a bond or empty space means "this is now the
		// selection" - so the annotations selected by the handlers upstream of this one have to go,
		// or an orbital picked a minute ago stays selected forever with no way to click it off.
		// Ctrl-click keeps them: that is the "add to what I already have" gesture.
		void ClearAnnotationSelections(RendererWindowState &windowState)
		{
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedVacancies.clear();
			windowState.defectFrameSelected = false;
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
			windowState.selectedSceneHideVolumes.clear();
		}
	} // namespace

	std::optional<ViewportPickRay> ComputeViewportPickRay(
		const RendererWindowState &window, glm::vec2 position, glm::vec2 imageSize)
	{
		if (!window.camera || imageSize.x <= 0 || imageSize.y <= 0) return std::nullopt;
		const glm::vec2 ndc(2.0f * position.x / imageSize.x - 1.0f, 1.0f - 2.0f * position.y / imageSize.y);
		const auto inverse = glm::inverse(window.camera->ProjectionMatrix() * window.camera->ViewMatrix());
		const auto nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
		const auto farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f) return std::nullopt;
		const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
		const glm::vec3 direction = glm::vec3(farPoint) / farPoint.w - origin;
		const float length = glm::length(direction);
		if (!std::isfinite(length) || length <= 1e-6f) return std::nullopt;
		return ViewportPickRay{origin - window.viewOffset, direction / length};
	}

	std::optional<std::size_t> PickBondAlongRay(
		const RendererWindowState &window, const glm::vec3 &origin, const glm::vec3 &direction)
	{
		const float length = glm::length(direction);
		if (!window.showBonds || !std::isfinite(length) || length <= 0) return std::nullopt;
		const glm::vec3 rayDirection = direction / length;
		std::optional<std::size_t> hit;
		float best = std::numeric_limits<float>::max();
		for (std::size_t index = 0; index < window.structure.bonds.size(); ++index)
		{
			const auto &bond = window.structure.bonds[index];
			if (!bond.visible || bond.firstAtomIndex >= window.structure.atoms.size() ||
				bond.secondAtomIndex >= window.structure.atoms.size()) continue;
			const auto &first = window.structure.atoms[bond.firstAtomIndex];
			const auto &second = window.structure.atoms[bond.secondAtomIndex];
			if (!first.visible || !second.visible) continue;
			float distance = 0;
			glm::vec3 closest(0);
			SelectionHitTest::ClosestPointsRaySegment(origin, rayDirection, first.cartesianPosition,
				second.cartesianPosition + bond.secondAtomPeriodicOffset, distance, closest);
			if (distance <= 0.001f || distance >= best) continue;
			if (glm::distance(origin + rayDirection * distance, closest) <= std::max(bond.radius * 2.5f, 0.12f))
			{
				best = distance;
				hit = index;
			}
		}
		return hit;
	}

	std::optional<std::size_t> PickAtomAlongRay(
		const RendererWindowState &windowState, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection)
	{
		const float length = glm::length(rayDirection);
		if (!std::isfinite(length) || length <= 0.0f)
			return std::nullopt;
		const glm::vec3 rayDir = rayDirection / length;
		float bestT = std::numeric_limits<float>::max();
		std::optional<std::size_t> hit;
		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			const RendererAtomData &atom = windowState.structure.atoms[i];
			if (!atom.visible || !windowState.showAtoms)
				continue;
			const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
			const float b = 2.0f * glm::dot(oc, rayDir);
			// Padded ~35% past the visible sphere - clicking exactly on a rendered edge (anti-
			// aliasing, small atoms like H) otherwise misses more often than it should.
			const float pickRadius = atom.radius * 1.35f;
			const float c = glm::dot(oc, oc) - pickRadius * pickRadius;
			const float disc = b * b - 4.0f * c;
			if (disc < 0.0f)
				continue;
			const float t = (-b - std::sqrt(disc)) / 2.0f;
			if (t > 0.001f && t < bestT)
			{
				bestT = t;
				hit = i;
			}
		}
		return hit;
	}

	void HandleAtomPick(
		RendererWindowState &windowState, float relX, float relY, bool additive, RendererLayer &layer)
	{
		if (!windowState.pickAtoms || !windowState.showAtoms)
			return;
		if (!windowState.camera || windowState.structure.atoms.empty())
			return;

		const auto ray = ComputeViewportPickRay(windowState, glm::vec2(relX, relY), windowState.viewportSize);
		if (!ray) return;
		const glm::vec3 rayOrigin = ray->origin, rayDir = ray->direction;

		const std::optional<std::size_t> hit = PickAtomAlongRay(windowState, rayOrigin, rayDir);
		if (!hit)
		{
			Ref<EventBus> eventBus = layer.GetEventBus();
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::AtomSelectionRequested event;
				event.windowId = windowState.windowId;
				event.additive = additive;
				eventBus->Publish(event);
			}
			return;
		}

		Ref<EventBus> eventBus = layer.GetEventBus();
		if (eventBus != nullptr)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.atomIndex = *hit;
			event.additive = additive;
			eventBus->Publish(event);
		}
	}

	void HandleViewportPick(
		RendererWindowState &windowState, float relX, float relY, bool additive, RendererLayer &layer)
	{
		if (!windowState.camera)
			return;

		const auto ray = ComputeViewportPickRay(windowState, glm::vec2(relX, relY), windowState.viewportSize);
		if (!ray) return;
		const glm::vec3 rayOrigin = ray->origin, rayDir = ray->direction;

		float bestAtomT = std::numeric_limits<float>::max();
		std::size_t hitAtomIndex = std::numeric_limits<std::size_t>::max();
		if (windowState.pickAtoms && windowState.showAtoms)
		{
			for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
			{
				const RendererAtomData &atom = windowState.structure.atoms[i];
				if (!atom.visible || !windowState.showAtoms)
					continue;
				const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
				const float a = glm::dot(rayDir, rayDir);
				const float b = 2.0f * glm::dot(oc, rayDir);
				const float pickRadius = atom.radius * 1.35f;
				const float c = glm::dot(oc, oc) - pickRadius * pickRadius;
				const float disc = b * b - 4.0f * a * c;
				if (disc < 0.0f)
					continue;
				const float t = (-b - std::sqrt(disc)) / (2.0f * a);
				if (t > 0.001f && t < bestAtomT)
				{
					bestAtomT = t;
					hitAtomIndex = i;
				}
			}
		}

		float bestBondT = std::numeric_limits<float>::max();
		std::size_t hitBondIndex = std::numeric_limits<std::size_t>::max();
		if (windowState.pickBonds)
			if (const auto hit = PickBondAlongRay(windowState, rayOrigin, rayDir))
			{
				hitBondIndex = *hit;
				const auto &bond = windowState.structure.bonds[*hit];
				glm::vec3 closest(0);
				SelectionHitTest::ClosestPointsRaySegment(rayOrigin, rayDir,
					windowState.structure.atoms[bond.firstAtomIndex].cartesianPosition,
					windowState.structure.atoms[bond.secondAtomIndex].cartesianPosition + bond.secondAtomPeriodicOffset,
					bestBondT, closest);
			}

		// Whichever hit is closer to the camera wins (atom keeps priority on an exact tie) - not "an
		// atom always wins if its inflated pick sphere was touched at all", which used to make a bond
		// unselectable whenever its own click point also fell inside a same-ray atom sphere further
		// along, even if the bond surface was the nearer of the two along the ray.
		const bool atomHit = hitAtomIndex != std::numeric_limits<std::size_t>::max();
		const bool bondHit = hitBondIndex != std::numeric_limits<std::size_t>::max();
		// Empty space is the gesture that means "deselect everything"; landing on an atom or a bond
		// is not. Clearing on an atom hit as well made the arrow anchor/colour actions unreachable -
		// the buttons need an arrow and its two atoms selected at once, and picking the atoms threw the arrow
		// away. Atoms and annotations coexist in the selection; SceneTransform decides which of
		// them the gizmo actually moves.
		if (!additive && !atomHit && !bondHit)
			ClearAnnotationSelections(windowState);
		if (atomHit && (!bondHit || bestAtomT <= bestBondT))
		{
			HandleAtomPick(windowState, relX, relY, additive, layer);
			return;
		}

		Ref<EventBus> eventBus = layer.GetEventBus();
		if (eventBus == nullptr)
			return;

		if (!bondHit)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.additive = additive;
			eventBus->Publish(event);
			return;
		}

		RendererEvents::Viewport::BondSelectionRequested event;
		event.windowId = windowState.windowId;
		event.bondIndex = hitBondIndex;
		event.additive = additive;
		eventBus->Publish(event);
	}
	// Ray-casts relX/relY (viewport-relative pixels) into the scene: snaps to the picked atom if the
	// click landed on one (same ray/pick-radius as HandleAtomPick), otherwise drops onto the plane
	// through the camera's orbit target, perpendicular to the view direction - a reasonable depth
	// for "wherever you clicked in empty space" without needing real scene-depth picking. Shared by
	// the 3D-cursor tool click and the viewport context menu's "Set 3D cursor here".
	glm::vec3 ComputeViewportWorldPosition(const RendererWindowState &windowState, float relX, float relY)
	{
		const auto ray = ComputeViewportPickRay(windowState, glm::vec2(relX, relY), windowState.viewportSize);
		if (!ray) return glm::vec3(0);
		const glm::vec3 rayOrigin = ray->origin, rayDir = ray->direction;

		if (const auto atom = PickAtomAlongRay(windowState, rayOrigin, rayDir))
			return windowState.structure.atoms[*atom].cartesianPosition;
		glm::vec3 hitPosition(0.0f);
		{
			const glm::vec3 target = windowState.camera->Target() - windowState.viewOffset;
			const glm::vec3 forward = glm::normalize(target - rayOrigin);
			const float denom = glm::dot(rayDir, forward);
			const float planeT = std::abs(denom) > 0.0001f ? glm::dot(target - rayOrigin, forward) / denom : 0.0f;
			hitPosition = rayOrigin + rayDir * planeT;
		}
		return hitPosition;
	}

} // namespace DefectStudio
