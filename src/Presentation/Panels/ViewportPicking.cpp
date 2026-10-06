#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportPicking.hpp"

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
		}
	} // namespace

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

		const float vpW = windowState.viewportSize.x;
		const float vpH = windowState.viewportSize.y;
		if (vpW <= 0.0f || vpH <= 0.0f)
			return;

		const float ndcX = (2.0f * relX / vpW) - 1.0f;
		const float ndcY = -((2.0f * relY / vpH) - 1.0f);

		const glm::mat4 invVP = glm::inverse(
			windowState.camera->ProjectionMatrix() *
			windowState.camera->ViewMatrix());

		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

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

		const float vpW = windowState.viewportSize.x;
		const float vpH = windowState.viewportSize.y;
		if (vpW <= 0.0f || vpH <= 0.0f)
			return;

		const float ndcX = (2.0f * relX / vpW) - 1.0f;
		const float ndcY = -((2.0f * relY / vpH) - 1.0f);

		const glm::mat4 invVP = glm::inverse(
			windowState.camera->ProjectionMatrix() *
			windowState.camera->ViewMatrix());

		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

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
		if (windowState.pickBonds && windowState.showBonds)
		{
			for (std::size_t i = 0; i < windowState.structure.bonds.size(); ++i)
			{
				const RendererBondData &bond = windowState.structure.bonds[i];
				if (!bond.visible || bond.firstAtomIndex >= windowState.structure.atoms.size() ||
					bond.secondAtomIndex >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &firstAtom = windowState.structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &secondAtom = windowState.structure.atoms[bond.secondAtomIndex];
				if (!firstAtom.visible || !secondAtom.visible)
					continue;

				float t = 0.0f;
				glm::vec3 closestOnSegment(0.0f);
				SelectionHitTest::ClosestPointsRaySegment(
					rayOrigin, rayDir, firstAtom.cartesianPosition,
					secondAtom.cartesianPosition + bond.secondAtomPeriodicOffset, t, closestOnSegment);
				if (t <= 0.001f || t >= bestBondT)
					continue;

				// Padded well past the rendered cylinder radius - bonds are thin, and unlike an
				// atom's sphere there's no natural "click anywhere on the visible disc" target to aim
				// for.
				const float pickRadius = std::max(bond.radius * 2.5f, 0.12f);
				const glm::vec3 closestOnRay = rayOrigin + rayDir * t;
				if (glm::distance(closestOnRay, closestOnSegment) <= pickRadius)
				{
					bestBondT = t;
					hitBondIndex = i;
				}
			}
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
		if (!windowState.camera || windowState.viewportSize.x <= 0.0f || windowState.viewportSize.y <= 0.0f)
			return glm::vec3(0.0f);

		const float ndcX = (2.0f * relX / windowState.viewportSize.x) - 1.0f;
		const float ndcY = -((2.0f * relY / windowState.viewportSize.y) - 1.0f);

		const glm::mat4 invVP = glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

		if (const auto atom = PickAtomAlongRay(windowState, rayOrigin, rayDir))
			return windowState.structure.atoms[*atom].cartesianPosition;
		glm::vec3 hitPosition(0.0f);
		{
			const glm::vec3 forward = glm::normalize(windowState.camera->Target() - rayOrigin);
			const float denom = glm::dot(rayDir, forward);
			const float planeT = std::abs(denom) > 0.0001f ? glm::dot(windowState.camera->Target() - rayOrigin, forward) / denom : 0.0f;
			hitPosition = rayOrigin + rayDir * planeT;
		}
		return hitPosition;
	}

} // namespace DefectStudio
