// Box- and circle-select and the screen-space hit-tests they run, lifted out of RendererPanel:
// the three-pane creation window is not a RendererPanel window, so as members these simply did
// not exist there - the same regression shape as the keybindings, the picking and the gizmo
// before them. Pure moves; the only change is RendererLayer arriving as a parameter.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"
#include "Renderer/Scene/SceneSelection.hpp"

namespace DefectStudio
{
	namespace
	{
		// Resolves the same anchor point renderLabels() draws a pin at (bond midpoint / angle vertex),
		// worldOffset already applied - ignores the bond's periodic-image shift for a 2-atom pin (only
		// matters for bonds crossing a periodic cell boundary), fine for a hit-test. Shared by the
		// region-select (box/circle) hit-testers below; HandlePinnedMeasurementInteraction keeps its
		// own equivalent local lambda since it predates this and touching working click/drag code for
		// a pure de-dup isn't worth the risk.
		[[nodiscard]] bool ResolvePinnedMeasurementAnchor(
			const RendererStructureData &structure, const RendererWindowState::PinnedMeasurement &pin,
			glm::vec3 &outAnchor)
		{
			const bool inRange = std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(), [&](const std::size_t index) {
				return index < structure.atoms.size();
			});
			if (!inRange)
				return false;
			if (pin.atomIndices.size() == 2)
			{
				outAnchor =
					(structure.atoms[pin.atomIndices[0]].cartesianPosition +
						structure.atoms[pin.atomIndices[1]].cartesianPosition) *
					0.5f;
			}
			else if (pin.atomIndices.size() == 3)
			{
				const std::size_t vertexIndex = ResolveAngleVertexIndex(structure, pin.atomIndices);
				outAnchor = structure.atoms[vertexIndex].cartesianPosition;
			}
			else
			{
				return false;
			}
			outAnchor += pin.worldOffset;
			return true;
		}

	} // namespace

	void HandleBoxSelectDrag(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered, RendererLayer &layer)
	{
		const ImVec2 mousePos = ImGui::GetMousePos();
		const glm::vec2 relativeMouse(mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);

		if (!windowState.selectionDragActive)
		{
			if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				return;
			windowState.selectionDragActive = true;
			windowState.selectionDragStart = relativeMouse;
			windowState.selectionDragCurrent = relativeMouse;
			return;
		}

		windowState.selectionDragCurrent = relativeMouse;
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			windowState.selectionDragActive = false;
			const glm::vec2 rectMin(
				std::min(windowState.selectionDragStart.x, windowState.selectionDragCurrent.x),
				std::min(windowState.selectionDragStart.y, windowState.selectionDragCurrent.y));
			const glm::vec2 rectMax(
				std::max(windowState.selectionDragStart.x, windowState.selectionDragCurrent.x),
				std::max(windowState.selectionDragStart.y, windowState.selectionDragCurrent.y));

			ImGuiIO &io = ImGui::GetIO();
			const RendererEvents::Viewport::RegionSelectMode mode = ResolveRegionSelectMode(io.KeyShift, io.KeyCtrl);
			if (windowState.pathEdit.IsActive())
			{
				ApplyPathElementRectSelection(windowState, rectMin, rectMax, mode);
				return;
			}
			// Gated on pickAtoms/pickBonds the same way HandleViewportPick's plain click already is -
			// box-select previously always matched atoms regardless of the active selection mode, and
			// never matched bonds at all even when the mode allowed picking them.
			PublishRegionSelection(
				windowState,
				(windowState.pickAtoms && windowState.showAtoms) ? HitTestRect(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				(windowState.pickBonds && windowState.showBonds) ? HitTestRectBonds(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				mode,
				layer);
			// Paths are ordinary scene objects; only label hits use the label selection mask.
			ApplyLabelRegionSelection(windowState,
				windowState.pickLabels ? HitTestRectPinnedMeasurements(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				windowState.pickLabels ? HitTestRectFreeLabels(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				mode, HitTestRectScenePaths(windowState, rectMin, rectMax));
			ApplySceneDrawingRegionSelection(windowState, rectMin, rectMax, 0.0f,
				mode == RendererEvents::Viewport::RegionSelectMode::Replace,
				mode == RendererEvents::Viewport::RegionSelectMode::Subtract);
		}
	}

	// Circle-select is a live brush (Blender-style): holding the mouse button paints the
	// selection continuously as the brush follows the cursor (add by default), Shift held
	// switches the brush to subtract instead.
	void HandleCircleSelectDrag(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered, RendererLayer &layer)
	{
		if (!hovered)
			return;

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			return;

		const ImVec2 mousePos = ImGui::GetMousePos();
		const glm::vec2 center(mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);
		const ImGuiIO &io = ImGui::GetIO();
		const RendererEvents::Viewport::RegionSelectMode mode = io.KeyShift
			? RendererEvents::Viewport::RegionSelectMode::Subtract
			: RendererEvents::Viewport::RegionSelectMode::Add;
		if (windowState.pathEdit.IsActive())
		{
			ApplyPathElementCircleSelection(windowState, center, windowState.circleSelectRadius, mode);
			return;
		}

		PublishRegionSelection(
			windowState,
			(windowState.pickAtoms && windowState.showAtoms) ? HitTestCircle(windowState, center, windowState.circleSelectRadius)
								   : std::vector<std::size_t>{},
			(windowState.pickBonds && windowState.showBonds) ? HitTestCircleBonds(windowState, center, windowState.circleSelectRadius)
								   : std::vector<std::size_t>{},
			mode,
			layer);
		ApplyLabelRegionSelection(windowState,
			windowState.pickLabels ? HitTestCirclePinnedMeasurements(windowState, center, windowState.circleSelectRadius) : std::vector<std::size_t>{},
			windowState.pickLabels ? HitTestCircleFreeLabels(windowState, center, windowState.circleSelectRadius) : std::vector<std::size_t>{},
			mode, HitTestCircleScenePaths(windowState, center, windowState.circleSelectRadius));
		ApplySceneDrawingRegionSelection(windowState, center, center, windowState.circleSelectRadius, false,
			mode == RendererEvents::Viewport::RegionSelectMode::Subtract);
	}

	std::vector<std::size_t> HitTestRect(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			if (!windowState.structure.atoms[i].visible)
				continue;
			const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
				viewProjection, windowState.viewportSize, windowState.structure.atoms[i].cartesianPosition);
			if (screen.has_value() && SelectionHitTest::PointInRect(*screen, rectMin, rectMax))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestCircle(
		const RendererWindowState &windowState, glm::vec2 center, float radius)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			if (!windowState.structure.atoms[i].visible)
				continue;
			const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
				viewProjection, windowState.viewportSize, windowState.structure.atoms[i].cartesianPosition);
			if (screen.has_value() && SelectionHitTest::PointInCircle(*screen, center, radius))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestRectBonds(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
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
			const glm::vec3 midpoint =
				(firstAtom.cartesianPosition + secondAtom.cartesianPosition + bond.secondAtomPeriodicOffset) * 0.5f;
			const std::optional<glm::vec2> screen =
				SelectionHitTest::ProjectToScreen(viewProjection, windowState.viewportSize, midpoint);
			if (screen.has_value() && SelectionHitTest::PointInRect(*screen, rectMin, rectMax))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestCircleBonds(
		const RendererWindowState &windowState, glm::vec2 center, float radius)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
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
			const glm::vec3 midpoint =
				(firstAtom.cartesianPosition + secondAtom.cartesianPosition + bond.secondAtomPeriodicOffset) * 0.5f;
			const std::optional<glm::vec2> screen =
				SelectionHitTest::ProjectToScreen(viewProjection, windowState.viewportSize, midpoint);
			if (screen.has_value() && SelectionHitTest::PointInCircle(*screen, center, radius))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	// Label counterparts of HitTestRect/HitTestCircle above - same "return every match" shape now
	// that label selection is multi-select too (selectedPinnedMeasurements/selectedFreeLabels).
	std::vector<std::size_t> HitTestRectPinnedMeasurements(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		for (std::size_t i = 0; i < windowState.pinnedMeasurements.size(); ++i)
		{
			glm::vec3 anchor(0.0f);
			if (!windowState.pinnedMeasurements[i].visible || !ResolvePinnedMeasurementAnchor(windowState.structure, windowState.pinnedMeasurements[i], anchor))
				continue;
			const std::optional<glm::vec2> screen =
				SelectionHitTest::ProjectToScreen(viewProjection, windowState.viewportSize, anchor);
			if (screen.has_value() && SelectionHitTest::PointInRect(*screen, rectMin, rectMax))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestCirclePinnedMeasurements(
		const RendererWindowState &windowState, glm::vec2 center, float radius)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		for (std::size_t i = 0; i < windowState.pinnedMeasurements.size(); ++i)
		{
			glm::vec3 anchor(0.0f);
			if (!windowState.pinnedMeasurements[i].visible || !ResolvePinnedMeasurementAnchor(windowState.structure, windowState.pinnedMeasurements[i], anchor))
				continue;
			const std::optional<glm::vec2> screen =
				SelectionHitTest::ProjectToScreen(viewProjection, windowState.viewportSize, anchor);
			if (screen.has_value() && SelectionHitTest::PointInCircle(*screen, center, radius))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestRectFreeLabels(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		for (std::size_t i = 0; i < windowState.freeLabels.size(); ++i)
		{
			if (!windowState.freeLabels[i].visible) continue;
			const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
				viewProjection, windowState.viewportSize, windowState.freeLabels[i].worldPosition);
			if (screen.has_value() && SelectionHitTest::PointInRect(*screen, rectMin, rectMax))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestCircleFreeLabels(
		const RendererWindowState &windowState, glm::vec2 center, float radius)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		for (std::size_t i = 0; i < windowState.freeLabels.size(); ++i)
		{
			if (!windowState.freeLabels[i].visible) continue;
			const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
				viewProjection, windowState.viewportSize, windowState.freeLabels[i].worldPosition);
			if (screen.has_value() && SelectionHitTest::PointInCircle(*screen, center, radius))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	// Applies one box/circle-select result to the label/path selection vectors - mirrors
	// RendererLayer::onRegionSelectionRequested's atom/bond semantics exactly: Replace clears
	// everything first then adds every hit, Add only adds what isn't already there, Subtract only
	// removes what's found.
	void ApplyLabelRegionSelection(
		RendererWindowState &windowState, const std::vector<std::size_t> &pinnedHits,
		const std::vector<std::size_t> &freeHits,
		RendererEvents::Viewport::RegionSelectMode mode, const std::vector<SceneObjectId> &pathHits)
	{
		using RendererEvents::Viewport::RegionSelectMode;
		if (mode == RegionSelectMode::Replace)
		{
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedScenePaths.clear();
		}

		auto applyHits = [](auto &selection, const auto &objects, const std::vector<std::size_t> &hits, bool subtract) {
			for (const std::size_t hit : hits)
			{
				if (hit >= objects.size())
					continue;
				const SceneObjectId id = objects[hit].id;
				const auto existing = std::find(selection.begin(), selection.end(), id);
				if (subtract)
				{
					if (existing != selection.end())
						selection.erase(existing);
				}
				else if (existing == selection.end())
				{
					selection.push_back(id);
				}
			}
		};
		const bool subtract = mode == RegionSelectMode::Subtract;
		applyHits(windowState.selectedPinnedMeasurements, windowState.pinnedMeasurements, pinnedHits, subtract);
		applyHits(windowState.selectedFreeLabels, windowState.freeLabels, freeHits, subtract);
		for (const SceneObjectId id : pathHits)
		{
			const auto existing = std::find(windowState.selectedScenePaths.begin(), windowState.selectedScenePaths.end(), id);
			if (subtract)
			{
				if (existing != windowState.selectedScenePaths.end())
					windowState.selectedScenePaths.erase(existing);
			}
			else if (existing == windowState.selectedScenePaths.end())
				windowState.selectedScenePaths.push_back(id);
		}

		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
	}

	RendererEvents::Viewport::RegionSelectMode ResolveRegionSelectMode(bool additive, bool subtractive)
	{
		if (additive)
			return RendererEvents::Viewport::RegionSelectMode::Add;
		if (subtractive)
			return RendererEvents::Viewport::RegionSelectMode::Subtract;
		return RendererEvents::Viewport::RegionSelectMode::Replace;
	}

	void PublishRegionSelection(
		RendererWindowState &windowState,
		std::vector<std::size_t> atomIndices,
		std::vector<std::size_t> bondIndices,
		RendererEvents::Viewport::RegionSelectMode mode,
		RendererLayer &layer)
	{
		Ref<EventBus> eventBus = layer.GetEventBus();
		if (eventBus == nullptr)
			return;
		RendererEvents::Viewport::RegionSelectionRequested event;
		event.windowId = windowState.windowId;
		event.atomIndices = std::move(atomIndices);
		event.bondIndices = std::move(bondIndices);
		event.mode = mode;
		eventBus->Publish(event);
	}
} // namespace DefectStudio
