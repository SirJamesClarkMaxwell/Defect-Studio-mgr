#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::DockBuilderGetCentralNode - auto-dock new windows into it
#include <ImGuizmo.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kViewportMinSize = 64.0f;
		constexpr float kViewportMaxSize = 8192.0f;

		using PeriodicTableIndexRow = std::array<int, 18>;
		const std::array<PeriodicTableIndexRow, 7> kPeriodicTableElementIndices = {
			PeriodicTableIndexRow{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
			PeriodicTableIndexRow{3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 6, 7, 8, 9, 10},
			PeriodicTableIndexRow{11, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 14, 15, 16, 17, 18},
			PeriodicTableIndexRow{19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36},
			PeriodicTableIndexRow{37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54},
			PeriodicTableIndexRow{55, 56, 0, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86},
			PeriodicTableIndexRow{87, 88, 0, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118}};

		[[nodiscard]] float SanitizeViewportDimension(float value)
		{
			if (!std::isfinite(value))
				return 640.0f;
			return std::clamp(value, kViewportMinSize, kViewportMaxSize);
		}

	}

	RendererPanel::RendererPanel(
		RendererLayer &layer,
		Ref<EventBus> eventBus,
		WeakRef<ContextManager> contextManager,
		WeakRef<CommandRegistry> commandRegistry,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_EventBus(std::move(eventBus)),
		  m_ContextManager(std::move(contextManager)),
		  m_CommandRegistry(std::move(commandRegistry))
	{
	}

	Ref<IPanel> RendererPanel::Clone() const
	{
		return CreateRef<RendererPanel>(*this);
	}

	void RendererPanel::Render()
	{
		if (!IsVisible())
			return;

		render(m_Layer.GetLastDeltaTime());
	}

	void RendererPanel::render(float deltaTime)
	{
		if (!m_Layer.IsAttached())
			return;

		std::vector<std::string> windowsToClose;
		for (RendererWindowState &windowState : m_Layer.GetWindows())
			renderStructureWindow(windowState, deltaTime, windowsToClose);
		for (const std::string &windowId : windowsToClose)
			m_Layer.RemoveWindow(windowId);

		// drawPeriodicTableWindow();
		m_Layer.CollectProfilingData();
	}

	void RendererPanel::renderStructureWindow(
		RendererWindowState &windowState, float deltaTime, std::vector<std::string> &windowsToClose)
	{
		if (windowState.camera == nullptr)
			return;
		(void)deltaTime;

		// "###windowId" keeps ImGui's window identity (docking, focus, size/position) pinned to
		// the stable windowId regardless of the visible label - two windows that happen to share
		// a display name (e.g. both opened from a "singlet_HSE" leaf folder) no longer collide
		// into the same ImGui window, and renaming a window's title is safe.
		const std::string imguiWindowLabel = windowState.title + "###RendererWindow_" + windowState.windowId;

		if (!windowState.dockingInitialized)
		{
			windowState.dockingInitialized = true;
			// Looked up fresh (not cached) since dock node IDs can be reshuffled by manual
			// re-docking elsewhere in the layout - GetMainViewport()->ID is the same dockspace ID
			// ImGuiLayer passes to DockSpaceOverViewport every frame, so this always resolves the
			// real central node rather than a stale/guessed ID.
			if (ImGuiDockNode *centralNode = ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID))
				ImGui::SetNextWindowDockID(centralNode->ID, ImGuiCond_FirstUseEver);
		}

		bool windowOpen = true;
		const bool began = ImGui::Begin(imguiWindowLabel.c_str(), &windowOpen);

		const bool nowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (nowFocused != windowState.lastFocusedState)
		{
			windowState.lastFocusedState = nowFocused;
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::FocusChanged focusEvent;
				focusEvent.windowId = windowState.windowId;
				focusEvent.focused = nowFocused;
				eventBus->Publish(focusEvent);
			}
			onViewportFocusChanged(windowState.windowId, nowFocused);
		}
		if (!began)
		{
			ImGui::End();
			return;
		}
		if (!windowOpen)
		{
			windowsToClose.push_back(windowState.windowId);
			ImGui::End();
			return;
		}

		drawViewportToolbar(windowState);
		ImGui::Separator();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		m_Layer.SetViewportSize(
			windowState.windowId,
			glm::vec2(SanitizeViewportDimension(available.x), SanitizeViewportDimension(available.y)));
		const ImVec2 viewportSize(windowState.viewportSize.x, windowState.viewportSize.y);

		const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();

		const unsigned int textureId = m_Layer.RenderToFbo(
			windowState.windowId,
			windowState.structure,
			windowState,
			m_Layer.GetGlobalSettings());

		ImGui::Image(
			static_cast<ImTextureID>(static_cast<uintptr_t>(textureId)),
			viewportSize,
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));

		// T08.6.4: drop target for a WAVECAR dragged from ProjectTreePanel - see the payload's
		// producer there for why only WAVECAR (not POSCAR/CONTCAR) uses drag-drop at all.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_WAVECAR_PATH"))
			{
				Ref<EventBus> eventBus = m_Layer.GetEventBus();
				if (eventBus != nullptr)
				{
					RendererEvents::Viewport::WavecarDropped event;
					event.windowId = windowState.windowId;
					event.wavecarPath = std::filesystem::path(static_cast<const char *>(payload->Data));
					eventBus->Queue(event);
				}
				// ElectronicStructurePanel/OccupationDiagramPanel only show/poll whichever window is
				// the last-*focused* viewport (FindFocusedWindow()) - a drag-drop never clicks the
				// window, so without this the load happens silently and neither panel ever notices.
				// Brings this window's tab to front too if it was docked in a hidden tab - visual
				// confirmation something happened. Takes effect next frame (IsWindowFocused() at the
				// top of this function is already past for this frame), same one-frame lag already
				// accepted elsewhere in this panel.
				ImGui::SetWindowFocus();
			}
			ImGui::EndDragDropTarget();
		}

		const bool hovered = ImGui::IsItemHovered();

		renderTransformGizmo(windowState, imageOrigin, viewportSize);
		// A live gizmo drag takes exclusive control of the viewport for the frame - suppress
		// box/circle-select and atom-pick so dragging a handle doesn't also fire a click-select.
		// IsOver() (hover/hit-test, true the instant the cursor is on a handle) matters just as
		// much as IsUsing() (drag already active) here: without it, a click that lands on the
		// gizmo's hotspot but a frame before ImGuizmo's internal click-to-activate edge fires
		// falls through to handleAtomPick below, misses every atom (the handle isn't drawn over
		// one), and clears the selection - which deletes the gizmo itself next frame since
		// renderTransformGizmo() early-returns with no selection. Net effect without this: the
		// gizmo looked draggable but every attempt just deselected instead of moving anything.
		// windowState.fallbackGizmoDragging must be included too - ImGuizmo's own IsOver()/IsUsing()
		// never go true for the hand-rolled fallback drag (see renderTransformGizmo), so without this
		// the exact frame a fallback drag starts also falls through to handleAtomPick below and
		// re-picks whatever atom is nearest the cursor (which is off in space along the arrow, not on
		// the original selection) - selection silently jumps to a different atom mid-drag.
		const bool gizmoCapturing = ImGuizmo::IsUsing() || ImGuizmo::IsOver() || windowState.fallbackGizmoDragging;

		if (windowState.activeSelectionTool == SelectionToolMode::Box && windowState.selectionDragActive)
		{
			ImDrawList *drawList = ImGui::GetWindowDrawList();
			const ImVec2 start(
				imageOrigin.x + windowState.selectionDragStart.x,
				imageOrigin.y + windowState.selectionDragStart.y);
			const ImVec2 current(
				imageOrigin.x + windowState.selectionDragCurrent.x,
				imageOrigin.y + windowState.selectionDragCurrent.y);
			const ImVec2 rectMin(std::min(start.x, current.x), std::min(start.y, current.y));
			const ImVec2 rectMax(std::max(start.x, current.x), std::max(start.y, current.y));
			drawList->AddRectFilled(rectMin, rectMax, IM_COL32(255, 200, 60, 40));
			drawList->AddRect(rectMin, rectMax, IM_COL32(255, 200, 60, 255));
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::Circle && hovered)
		{
			// Brush cursor: always follows the live mouse position at the persistent,
			// scroll-adjustable radius - not a drag-defined shape like box-select.
			const ImVec2 mousePos = ImGui::GetMousePos();
			ImDrawList *drawList = ImGui::GetWindowDrawList();
			drawList->AddCircleFilled(mousePos, windowState.circleSelectRadius, IM_COL32(255, 200, 60, 40));
			drawList->AddCircle(mousePos, windowState.circleSelectRadius, IM_COL32(255, 200, 60, 255));
		}

		if (hovered && windowState.activeSelectionTool == SelectionToolMode::Circle)
		{
			ImGuiIO &io = ImGui::GetIO();
			if (io.MouseWheel != 0.0f)
			{
				constexpr float kRadiusStep = 6.0f;
				constexpr float kMinRadius = 6.0f;
				constexpr float kMaxRadius = 400.0f;
				windowState.circleSelectRadius = std::clamp(
					windowState.circleSelectRadius + io.MouseWheel * kRadiusStep, kMinRadius, kMaxRadius);
				io.MouseWheel = 0.0f;
			}
		}

		if (hovered && !gizmoCapturing)
			applyViewportInputNavigation(windowState, imageOrigin, deltaTime);
		else if (!hovered)
		{
			windowState.dragActive = false;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				m_Layer.CommitViewInteraction(windowState.windowId);
			}
		}

		if (gizmoCapturing)
		{
			// Nothing else consumes mouse input this frame.
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::Box)
		{
			handleBoxSelectDrag(windowState, imageOrigin, hovered);
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::Circle)
		{
			handleCircleSelectDrag(windowState, imageOrigin, hovered);
		}
		else if (hovered)
		{
			ImGuiIO &io = ImGui::GetIO();
			const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				!ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
				!io.KeyAlt;
			if (leftClicked)
			{
				const ImVec2 mousePos = ImGui::GetMousePos();
				const float relX = mousePos.x - imageOrigin.x;
				const float relY = mousePos.y - imageOrigin.y;
				if (relX >= 0.0f &&
					relY >= 0.0f &&
					relX < windowState.viewportSize.x &&
					relY < windowState.viewportSize.y)
				{
					handleAtomPick(windowState, relX, relY, io.KeyCtrl);
				}
			}
		}

		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
	}

	void RendererPanel::handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive)
	{
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

		float bestT = std::numeric_limits<float>::max();
		std::size_t hitIndex = std::numeric_limits<std::size_t>::max();

		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			const RendererAtomData &atom = windowState.structure.atoms[i];
			const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
			const float a = glm::dot(rayDir, rayDir);
			const float b = 2.0f * glm::dot(oc, rayDir);
			const float c = glm::dot(oc, oc) - atom.radius * atom.radius;
			const float disc = b * b - 4.0f * a * c;
			if (disc < 0.0f)
				continue;
			const float t = (-b - std::sqrt(disc)) / (2.0f * a);
			if (t > 0.001f && t < bestT)
			{
				bestT = t;
				hitIndex = i;
			}
		}

		if (hitIndex == std::numeric_limits<std::size_t>::max())
		{
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::AtomSelectionRequested event;
				event.windowId = windowState.windowId;
				event.additive = additive;
				eventBus->Publish(event);
			}
			return;
		}

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus != nullptr)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.atomIndex = hitIndex;
			event.additive = additive;
			eventBus->Publish(event);
		}
	}

	void RendererPanel::handleBoxSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered)
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
			publishRegionSelection(windowState, hitTestRect(windowState, rectMin, rectMax), resolveRegionSelectMode(io.KeyShift, io.KeyCtrl));
		}
	}

	// Circle-select is a live brush (Blender-style): holding the mouse button paints the
	// selection continuously as the brush follows the cursor (add by default), Shift held
	// switches the brush to subtract instead.
	void RendererPanel::handleCircleSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered)
	{
		if (!hovered)
			return;

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			return;

		const ImVec2 mousePos = ImGui::GetMousePos();
		const glm::vec2 center(mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);
		const ImGuiIO &io = ImGui::GetIO();

		publishRegionSelection(
			windowState,
			hitTestCircle(windowState, center, windowState.circleSelectRadius),
			io.KeyShift
				? RendererEvents::Viewport::RegionSelectMode::Subtract
				: RendererEvents::Viewport::RegionSelectMode::Add);
	}

	// G/R/S transform gizmo for the current selection. Pivot is the live centroid of selected
	// atoms, recomputed every frame (not cached) - stays correct under rotate/scale since a rigid
	// transform about its own centroid leaves that centroid fixed. While dragging, the frame's
	// incremental delta is applied directly to windowState.structure (renderer hot path) for
	// immediate visual feedback; on release, the final result is committed to the domain structure
	// as one undoable command (RendererAtomEditCommands::TransformSelectedAtomsCommand).
	void RendererPanel::renderTransformGizmo(RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (windowState.selectedAtomIndices.empty() || windowState.camera == nullptr)
		{
			windowState.gizmoDragActive = false;
			return;
		}

		glm::vec3 pivot(0.0f);
		for (const std::size_t atomIndex : windowState.selectedAtomIndices)
			pivot += windowState.structure.atoms[atomIndex].cartesianPosition;
		pivot /= static_cast<float>(windowState.selectedAtomIndices.size());

		glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), pivot);
		glm::mat4 deltaMatrix(1.0f);

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::mat4 projection = windowState.camera->ProjectionMatrix();

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		switch (windowState.gizmoOperation)
		{
			case GizmoOperation::Translate: operation = ImGuizmo::TRANSLATE; break;
			case GizmoOperation::Rotate: operation = ImGuizmo::ROTATE; break;
			case GizmoOperation::Scale: operation = ImGuizmo::SCALE; break;
		}

		ImGuizmo::PushID(windowState.windowId.c_str());
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetOrthographic(windowState.camera->Projection() == CameraProjection::Orthographic);
		ImGuizmo::SetRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y);
		// Default (0.1) reads as tiny/hard-to-grab against atom sphere sizes - bump handle size,
		// hit-testing scales with it automatically (ImGuizmo derives hitbox from the same factor).
		// Line/arrow thickness is a separate style knob (doesn't follow SetGizmoSizeClipSpace) -
		// bump those too so the arrows read clearly at this scale, not just the hit area.
		ImGuizmo::SetGizmoSizeClipSpace(0.28f);
		ImGuizmo::Style &gizmoStyle = ImGuizmo::GetStyle();
		gizmoStyle.TranslationLineThickness = 5.0f;
		gizmoStyle.TranslationLineArrowSize = 10.0f;
		gizmoStyle.RotationLineThickness = 4.0f;
		gizmoStyle.RotationOuterLineThickness = 4.0f;
		gizmoStyle.ScaleLineThickness = 5.0f;
		gizmoStyle.ScaleLineCircleSize = 8.0f;
		gizmoStyle.CenterCircleSize = 8.0f;
		ImGuizmo::Manipulate(
			glm::value_ptr(view),
			glm::value_ptr(projection),
			operation,
			ImGuizmo::WORLD,
			glm::value_ptr(gizmoMatrix),
			glm::value_ptr(deltaMatrix));
		ImGuizmo::PopID();

		// ImGuizmo's own screen-space picking (IsOver()/IsUsing()) is unreliable in this app - a
		// click squarely on a visibly-hovered handle routinely fails to activate a drag (confirmed
		// via synthetic clicks measured directly against screenshots, landing under a pixel off the
		// handle centerline). Degects-Studio - an earlier iteration of this project at
		// Desktop/STUDIA/Degects-Studio - hit the identical problem and shipped a hand-rolled
		// screen-space axis pick + drag as the actual interaction path, using ImGuizmo only to draw
		// the handles. Ported and simplified here: atoms only (no empties/lights), world-space axes
		// only (this gizmo never runs in LOCAL mode). Rotate isn't covered - its ring hit-test would
		// need ImGuizmo's internal ring radius, which isn't exposed - translate/scale cover the
		// reported bug.
		if (operation != ImGuizmo::ROTATE)
		{
			const glm::mat4 viewProjection = projection * view;
			auto projectToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool {
				const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
				if (clip.w <= 0.0001f)
					return false;
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				outScreen = glm::vec2(
					imageOrigin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
					imageOrigin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y);
				return true;
			};

			glm::vec2 pivotScreen(0.0f);
			if (projectToScreen(pivot, pivotScreen))
			{
				constexpr glm::vec3 kWorldAxes[3] = {
					glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
				// A 1-world-unit probe gives axis direction + a pixels-per-world ratio for this
				// frame's zoom. The pick test below is angle/distance-from-pivot based, not a
				// fixed-length segment, so it doesn't need to match ImGuizmo's own (inaccessible)
				// handle length.
				glm::vec2 axisScreenDir[3];
				float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
				bool axisValid[3] = {false, false, false};
				for (int axis = 0; axis < 3; ++axis)
				{
					glm::vec2 probeScreen;
					if (!projectToScreen(pivot + kWorldAxes[axis], probeScreen))
						continue;
					const glm::vec2 axisVec = probeScreen - pivotScreen;
					const float axisPixels = glm::length(axisVec);
					if (axisPixels < 1.0f)
						continue;
					axisScreenDir[axis] = axisVec / axisPixels;
					axisPixelsPerWorld[axis] = axisPixels;
					axisValid[axis] = true;
				}

				if (!windowState.fallbackGizmoDragging && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
					ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					constexpr float kPickMinDistance = 20.0f;
					constexpr float kPickMaxDistance = 350.0f;
					constexpr float kPickPerpTolerance = 16.0f;
					const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
					const glm::vec2 fromPivot = mousePos - pivotScreen;
					const float radial = glm::length(fromPivot);
					int hoveredAxis = -1;
					float bestPerp = kPickPerpTolerance;
					if (radial >= kPickMinDistance && radial <= kPickMaxDistance)
					{
						for (int axis = 0; axis < 3; ++axis)
						{
							if (!axisValid[axis])
								continue;
							const float along = glm::dot(fromPivot, axisScreenDir[axis]);
							if (along <= 0.0f)
								continue;
							const float perp = glm::length(fromPivot - axisScreenDir[axis] * along);
							if (perp < bestPerp)
							{
								bestPerp = perp;
								hoveredAxis = axis;
							}
						}
					}

					if (hoveredAxis >= 0)
					{
						windowState.fallbackGizmoDragging = true;
						windowState.fallbackGizmoAxis = hoveredAxis;
						windowState.fallbackLastMousePos = mousePos;
						windowState.fallbackDragAxisScreenDir = axisScreenDir[hoveredAxis];
						windowState.fallbackDragAxisWorldDir = kWorldAxes[hoveredAxis];
						windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
					}
				}
			}

			if (windowState.fallbackGizmoDragging)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
					const glm::vec2 delta = mousePos - windowState.fallbackLastMousePos;
					windowState.fallbackLastMousePos = mousePos;

					const float deltaOnAxisPixels = glm::dot(delta, windowState.fallbackDragAxisScreenDir);
					const float deltaOnAxisWorld = deltaOnAxisPixels / windowState.fallbackDragPixelsPerWorld;

					if (operation == ImGuizmo::SCALE)
					{
						const float factor = glm::clamp(1.0f + deltaOnAxisWorld, 0.05f, 20.0f);
						for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						{
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							RendererAtomData &atom = windowState.structure.atoms[atomIndex];
							const glm::vec3 relative = atom.cartesianPosition - pivot;
							const float along = glm::dot(relative, windowState.fallbackDragAxisWorldDir);
							const glm::vec3 perpendicular = relative - windowState.fallbackDragAxisWorldDir * along;
							atom.cartesianPosition =
								pivot + perpendicular + windowState.fallbackDragAxisWorldDir * (along * factor);
						}
					}
					else
					{
						const glm::vec3 worldDelta = windowState.fallbackDragAxisWorldDir * deltaOnAxisWorld;
						for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						{
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							windowState.structure.atoms[atomIndex].cartesianPosition += worldDelta;
						}
					}
					windowState.gizmoDragActive = true;
					return;
				}

				windowState.fallbackGizmoDragging = false;
				windowState.fallbackGizmoAxis = -1;
			}
		}

		if (ImGuizmo::IsUsing())
		{
			windowState.gizmoDragActive = true;
			for (const std::size_t atomIndex : windowState.selectedAtomIndices)
			{
				if (atomIndex >= windowState.structure.atoms.size())
					continue;
				RendererAtomData &atom = windowState.structure.atoms[atomIndex];
				atom.cartesianPosition = glm::vec3(deltaMatrix * glm::vec4(atom.cartesianPosition, 1.0f));
			}
			return;
		}

		if (!windowState.gizmoDragActive)
			return;

		windowState.gizmoDragActive = false;

		Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
		if (commandRegistry == nullptr)
			return;

		GizmoTransformPayload payload;
		payload.windowId = windowState.windowId;
		payload.atomIndices = windowState.selectedAtomIndices;
		payload.afterPositions.reserve(payload.atomIndices.size());
		for (const std::size_t atomIndex : payload.atomIndices)
			payload.afterPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
		payload.description = windowState.gizmoOperation == GizmoOperation::Translate ? "Move selected atoms"
			: windowState.gizmoOperation == GizmoOperation::Rotate                    ? "Rotate selected atoms"
																						: "Scale selected atoms";

		CommandContext context;
		context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
		Result<CommandOutcome> result = commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
		if (!result)
			DS_LOG_WARN("Gizmo transform commit failed: {}", result.Error().technicalDetails);
	}

	std::vector<std::size_t> RendererPanel::hitTestRect(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const
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

	std::vector<std::size_t> RendererPanel::hitTestCircle(
		const RendererWindowState &windowState, glm::vec2 center, float radius) const
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

	RendererEvents::Viewport::RegionSelectMode RendererPanel::resolveRegionSelectMode(bool additive, bool subtractive)
	{
		if (additive)
			return RendererEvents::Viewport::RegionSelectMode::Add;
		if (subtractive)
			return RendererEvents::Viewport::RegionSelectMode::Subtract;
		return RendererEvents::Viewport::RegionSelectMode::Replace;
	}

	void RendererPanel::publishRegionSelection(
		RendererWindowState &windowState,
		std::vector<std::size_t> atomIndices,
		RendererEvents::Viewport::RegionSelectMode mode)
	{
		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return;
		RendererEvents::Viewport::RegionSelectionRequested event;
		event.windowId = windowState.windowId;
		event.atomIndices = std::move(atomIndices);
		event.mode = mode;
		eventBus->Publish(event);
	}

	void RendererPanel::drawPeriodicTableWindow()
	{
		if (!m_Layer.GetShowPeriodicTableWindow())
			return;

		ImGui::SetNextWindowSize(ImVec2(760.0f, 430.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Periodic Table", &m_Layer.GetShowPeriodicTableWindow()))
		{
			ImGui::End();
			return;
		}

		const ImVec2 cellSize(36.0f, 32.0f);
		const auto &symbols = m_Layer.GetPeriodicTableSymbols();
		const auto &lanthanides = m_Layer.GetLanthanideSymbols();
		const auto &actinides = m_Layer.GetActinideSymbols();

		for (const PeriodicTableIndexRow &row : kPeriodicTableElementIndices)
		{
			for (std::size_t column = 0; column < row.size(); ++column)
			{
				const int atomicNumber = row[column];
				if (column > 0)
					ImGui::SameLine();

				if (atomicNumber <= 0 || static_cast<std::size_t>(atomicNumber) > symbols.size())
				{
					ImGui::Dummy(cellSize);
					continue;
				}

				const std::string &symbol = symbols[static_cast<std::size_t>(atomicNumber - 1)];
				if (symbol == m_Layer.GetSelectedPeriodicElement())
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.92f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.64f, 0.98f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.84f, 1.0f));
				}
				const bool clicked = ImGui::Button(symbol.c_str(), cellSize);
				if (symbol == m_Layer.GetSelectedPeriodicElement())
					ImGui::PopStyleColor(3);

				if (clicked)
					m_Layer.GetSelectedPeriodicElement() = symbol;
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Lanthanides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < lanthanides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(lanthanides[index].c_str(), cellSize))
				m_Layer.GetSelectedPeriodicElement() = lanthanides[index];
		}
		ImGui::TextUnformatted("Actinides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < actinides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(actinides[index].c_str(), cellSize))
				m_Layer.GetSelectedPeriodicElement() = actinides[index];
		}

		ImGui::Separator();
		ImGui::Text("Selected element: %s", m_Layer.GetSelectedPeriodicElement().c_str());

		ImGui::End();
	}

} // namespace DefectStudio
