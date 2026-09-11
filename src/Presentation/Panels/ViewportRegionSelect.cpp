// Box- and circle-select and the screen-space hit-tests they run, lifted out of RendererPanel:
// the three-pane creation window is not a RendererPanel window, so as members these simply did
// not exist there - the same regression shape as the keybindings, the picking and the gizmo
// before them. Pure moves; the only change is RendererLayer arriving as a parameter.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

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
			// Gated on pickAtoms/pickBonds the same way HandleViewportPick's plain click already is -
			// box-select previously always matched atoms regardless of the active selection mode, and
			// never matched bonds at all even when the mode allowed picking them.
			PublishRegionSelection(
				windowState,
				windowState.pickAtoms ? HitTestRect(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				windowState.pickBonds ? HitTestRectBonds(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				mode,
				layer);
			// Labels aren't part of the atom/bond region-select event above (single-select fields, not
			// an entity list) - applied directly here instead, same pickLabels gate as everywhere else.
			if (windowState.pickLabels)
			{
				ApplyLabelRegionSelection(
					windowState, HitTestRectPinnedMeasurements(windowState, rectMin, rectMax),
					HitTestRectFreeLabels(windowState, rectMin, rectMax),
					HitTestRectSceneArrows(windowState, rectMin, rectMax), mode);
			}
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

		PublishRegionSelection(
			windowState,
			windowState.pickAtoms ? HitTestCircle(windowState, center, windowState.circleSelectRadius)
								   : std::vector<std::size_t>{},
			windowState.pickBonds ? HitTestCircleBonds(windowState, center, windowState.circleSelectRadius)
								   : std::vector<std::size_t>{},
			mode,
			layer);
		if (windowState.pickLabels)
		{
			ApplyLabelRegionSelection(
				windowState, HitTestCirclePinnedMeasurements(windowState, center, windowState.circleSelectRadius),
				HitTestCircleFreeLabels(windowState, center, windowState.circleSelectRadius),
				HitTestCircleSceneArrows(windowState, center, windowState.circleSelectRadius), mode);
		}
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
			if (!ResolvePinnedMeasurementAnchor(windowState.structure, windowState.pinnedMeasurements[i], anchor))
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
			if (!ResolvePinnedMeasurementAnchor(windowState.structure, windowState.pinnedMeasurements[i], anchor))
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
			const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
				viewProjection, windowState.viewportSize, windowState.freeLabels[i].worldPosition);
			if (screen.has_value() && SelectionHitTest::PointInCircle(*screen, center, radius))
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	// sceneArrows counterpart of the point-based hit-testers above - "hit" means the start, end, or
	// midpoint screen-projects into the rect/circle (good enough for a box/circle-select convenience
	// feature, not full segment-vs-region clipping).
	std::vector<std::size_t> HitTestRectSceneArrows(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		// 5 evenly-spaced samples along the segment, not just start/end/mid - catches a long arrow
		// crossing the region without either endpoint or its exact midpoint landing inside it (docs/
		// scene_arrow_rework_plan_corrected.md Step 9's "simple sampling" option).
		constexpr std::array<float, 5> kSampleT = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
		for (std::size_t i = 0; i < windowState.sceneArrows.size(); ++i)
		{
			const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[i];
			bool hit = false;
			for (const float t : kSampleT)
			{
				const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
					viewProjection, windowState.viewportSize, glm::mix(arrow.start, arrow.end, t));
				if (screen.has_value() && SelectionHitTest::PointInRect(*screen, rectMin, rectMax))
				{
					hit = true;
					break;
				}
			}
			if (hit)
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	std::vector<std::size_t> HitTestCircleSceneArrows(
		const RendererWindowState &windowState, glm::vec2 center, float radius)
	{
		std::vector<std::size_t> hitIndices;
		if (windowState.camera == nullptr)
			return hitIndices;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		constexpr std::array<float, 5> kSampleT = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
		for (std::size_t i = 0; i < windowState.sceneArrows.size(); ++i)
		{
			const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[i];
			bool hit = false;
			for (const float t : kSampleT)
			{
				const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
					viewProjection, windowState.viewportSize, glm::mix(arrow.start, arrow.end, t));
				if (screen.has_value() && SelectionHitTest::PointInCircle(*screen, center, radius))
				{
					hit = true;
					break;
				}
			}
			if (hit)
				hitIndices.push_back(i);
		}
		return hitIndices;
	}

	// Applies one box/circle-select result to the label/arrow selection vectors - mirrors
	// RendererLayer::onRegionSelectionRequested's atom/bond semantics exactly: Replace clears
	// everything first then adds every hit, Add only adds what isn't already there, Subtract only
	// removes what's found.
	void ApplyLabelRegionSelection(
		RendererWindowState &windowState, const std::vector<std::size_t> &pinnedHits,
		const std::vector<std::size_t> &freeHits, const std::vector<std::size_t> &arrowHits,
		RendererEvents::Viewport::RegionSelectMode mode)
	{
		using RendererEvents::Viewport::RegionSelectMode;
		if (mode == RegionSelectMode::Replace)
		{
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedSceneArrows.clear();
		}

		auto applyHits = [](std::vector<std::size_t> &selection, const std::vector<std::size_t> &hits, bool subtract) {
			for (const std::size_t hit : hits)
			{
				const auto existing = std::find(selection.begin(), selection.end(), hit);
				if (subtract)
				{
					if (existing != selection.end())
						selection.erase(existing);
				}
				else if (existing == selection.end())
				{
					selection.push_back(hit);
				}
			}
		};
		const bool subtract = mode == RegionSelectMode::Subtract;
		applyHits(windowState.selectedPinnedMeasurements, pinnedHits, subtract);
		applyHits(windowState.selectedFreeLabels, freeHits, subtract);
		applyHits(windowState.selectedSceneArrows, arrowHits, subtract);

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
