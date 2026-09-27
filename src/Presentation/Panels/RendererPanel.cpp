#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportInput.hpp"
#include "Presentation/Panels/ViewportInteraction.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportPathOverlay.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>

#include "Domain/DomainLayer.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

#include <glm/gtc/constants.hpp>
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
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ViewportSidePanel.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneVisibility.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	[[nodiscard]] static std::size_t ArrowIndex(const RendererWindowState &windowState, const SceneObjectId id)
	{
		const auto found = std::find_if(windowState.sceneArrows.begin(), windowState.sceneArrows.end(), [id](const auto &arrow) { return arrow.id == id; });
		return found == windowState.sceneArrows.end() ? windowState.sceneArrows.size() : static_cast<std::size_t>(std::distance(windowState.sceneArrows.begin(), found));
	}

	namespace
	{
		constexpr float kViewportMinSize = 64.0f;
		constexpr float kViewportMaxSize = 8192.0f;

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
		WeakRef<DomainLayer> domainLayer,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_EventBus(std::move(eventBus)),
		  m_ContextManager(std::move(contextManager)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_DomainLayer(std::move(domainLayer))
	{
		if (Ref<CommandRegistry> registry = m_CommandRegistry.lock())
			RegisterViewportSceneObjectCommands(*registry, m_Layer);
		bindWindowEvents();
	}

	RendererPanel::RendererPanel(const RendererPanel &other)
		: IPanel(other.GetTitle(), other.IsVisible()),
		  m_Layer(other.m_Layer),
		  m_EventBus(other.m_EventBus),
		  m_ContextManager(other.m_ContextManager),
		  m_CommandRegistry(other.m_CommandRegistry),
		  m_DomainLayer(other.m_DomainLayer),
		  m_LastMousePositions(other.m_LastMousePositions),
		  m_ContextMenuWorldPosition(other.m_ContextMenuWorldPosition),
		  m_AddAtomPopupRequested(other.m_AddAtomPopupRequested),
		  m_AddAtomPopupWindowId(other.m_AddAtomPopupWindowId),
		  m_AddAtomPopupFractional(other.m_AddAtomPopupFractional),
		  m_AddAtomPopupPosition(other.m_AddAtomPopupPosition),
		  m_AddMenuRequested(other.m_AddMenuRequested),
		  m_AddMenuWindowId(other.m_AddMenuWindowId),
		  m_AddMenuPosition(other.m_AddMenuPosition),
		  m_AddMenuPositionFractional(other.m_AddMenuPositionFractional),
		  m_AddMenuScreenPos(other.m_AddMenuScreenPos)
	{
		// m_TabClose and the active viewport rectangle are deliberately NOT copied: a half-answered
		// close prompt and a rectangle measured in another panel's frame both belong to the instance
		// that produced them.
		bindWindowEvents();
	}

	void RendererPanel::bindWindowEvents()
	{
		if (m_EventBus == nullptr)
			return;
		AddSubscription(m_EventBus->Subscribe<RendererEvents::Windows::CloseRequested>(
			[this](const RendererEvents::Windows::CloseRequested &event)
			{
				m_TabClose.Request(event.windowId);
			}));
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

		const std::string activeWindowId = ResolveActiveRendererWindowId(m_Layer);

		consumeAddMenuRequests();
		std::vector<std::string> windowsToClose;
		for (RendererWindowState &windowState : m_Layer.GetWindows())
		{
			// Creation-session windows are panes of StructureCreationTabsPanel's single three-pane
			// window, not ImGui windows of their own - they exist here only for their camera and
			// FBO. Drawing them from this loop would put three stray title bars on screen.
			if (!windowState.sessionId.empty())
				continue;
			renderStructureWindow(windowState, deltaTime, windowsToClose, activeWindowId);
		}
		m_TabClose.Drain(m_Layer, windowsToClose);
		for (const std::string &windowId : windowsToClose)
			m_Layer.RemoveWindow(windowId);
		if (m_EventBus != nullptr)
			DrawRendererTabBarAddButton(m_Layer, *m_EventBus);
		consumeAddMenuRequests();

		drawPeriodicTableWindow();
		drawAddMenu();
		drawAddAtomPopup();
		m_Layer.CollectProfilingData();
	}

	void RendererPanel::renderStructureWindow(
		RendererWindowState &windowState,
		float deltaTime,
		std::vector<std::string> &windowsToClose,
		const std::string &activeWindowId)
	{
		if (windowState.camera == nullptr)
			return;
		(void)deltaTime;

		// "###windowId" keeps ImGui's window identity (docking, focus, size/position) pinned to
		// the stable windowId regardless of the visible label - two windows that happen to share
		// a display name (e.g. both opened from a "singlet_HSE" leaf folder) no longer collide
		// into the same ImGui window, and renaming a window's title is safe.
		std::string displayTitle = windowState.title;

		// Append "*" if structure is dirty (revision != savedRevision)
		if (auto domainLayer = m_DomainLayer.lock())
		{
			if (auto structureRecord = domainLayer->Workspace().Structures().Find(windowState.structureId).lock())
			{
				if (structureRecord->revision != structureRecord->savedRevision)
					displayTitle += "*";
			}
		}

		const std::string imguiWindowLabel = displayTitle + "###RendererWindow_" + windowState.windowId;

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

		UpdateViewportFocusState(
			windowState, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), m_Layer, m_ContextManager);
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

		// Read BEFORE the overlays. IsItemHovered() answers about the last submitted item, and
		// EndChild submits one, so every overlay drawn between the image and this call would move the
		// question from "is the cursor over the viewport" to "is it over the last toolbar" - which is
		// false almost everywhere and silently kills every click the viewport handles.
		const bool hovered = ImGui::IsItemHovered();

		const ImVec2 cursorAfterImage = ImGui::GetCursorScreenPos();
		const float horizontalToolbarOffset = windowState.windowId == activeWindowId
			? DrawViewportToolbarOverlays(windowState, m_Layer, imageOrigin, viewportSize)
			: 0.0f;
		DrawViewportPathOverlay(windowState, m_Layer.GetGlobalSettings(), imageOrigin, viewportSize);
		ImGui::SetCursorScreenPos(cursorAfterImage);

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

		// Escape always deselects, regardless of how a click landed you in this state - a reliable
		// way out when the gizmo's screen-space pick band swallows a click meant to clear selection
		// (the gizmo disappears once nothing is selected, since RenderTransformGizmo() early-returns
		// with an empty selection). Doesn't try to cancel/revert a drag already in progress - only
		// acts when nothing is actively being dragged, so it can't leave a transform half-applied.
		if (hovered && !windowState.pathEdit.IsActive() && !windowState.modalTransform.has_value() && !windowState.pinnedMeasurementDragging &&
			!windowState.freeLabelDragging && !windowState.sceneArrowDragging &&
			!windowState.selectionDragActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedSceneArrows.clear();
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
			windowState.sceneArrowQuickEditActive = false;
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::AtomSelectionRequested event;
				event.windowId = windowState.windowId;
				event.additive = false;
				eventBus->Publish(event);
			}
		}

		// RenderTransformGizmo() returns whether it's hovered/dragging using OUR OWN screen-space
		// hit-test, not ImGuizmo::IsOver()/IsUsing() - those proved unreliable in both directions
		// (see the big comment inside RenderTransformGizmo): sometimes falsely true, which blocked
		// our fallback pick from ever starting so the click did nothing; sometimes falsely false
		// right as a fallback drag begins, letting HandleAtomPick fire on the same frame and
		// silently re-pick whichever atom is nearest the cursor (off in space along the arrow, not
		// the original selection) - selection jumping mid-drag. A live gizmo drag/hover takes
		// exclusive control of the viewport for the frame - suppress box/circle-select and
		// atom-pick so grabbing a handle doesn't also fire a click-select underneath it.
		// Label transforms, the keyboard-only pin shortcuts and the short-circuiting gizmo chain, in
		// the one order they work in - shared with the creation panes (ViewportInteraction.hpp).
		const bool gizmoCapturing =
			RunViewportGizmoChain(
				windowState, imageOrigin, viewportSize, hovered, horizontalToolbarOffset, m_Layer, m_CommandRegistry);

		renderViewportContextMenu(windowState, imageOrigin, viewportSize, hovered);
		renderSceneArrowQuickEditPanel(windowState, imageOrigin, viewportSize);

		// Visible arrow handles use the exact geometry RenderTransformGizmo hit-tests earlier in the
		// frame. Multi-selection keeps endpoint-only markers; a sole translated arrow also exposes its
		// whole-arrow midpoint target.
		DrawSceneArrowHandleMarkers(windowState, imageOrigin, viewportSize);

		// Box/circle overlay, the brush's scroll-wheel radius, and the drag dispatch. Runs before
		// navigation below because the circle brush eats the wheel event the camera would otherwise
		// zoom with.
		const bool selectionToolConsumedMouse =
			DrawAndDispatchSelectionTools(windowState, imageOrigin, hovered, gizmoCapturing, m_Layer);

		ApplyContinuousKeyboardNudge(windowState, deltaTime, m_Layer, m_CommandRegistry);
		CommitFinishedKeyboardViewInteraction(windowState, m_Layer);
		ApplyContinuousKeyboardPan(windowState, deltaTime, m_Layer);

		if (hovered && !gizmoCapturing)
			ApplyViewportInputNavigation(windowState, deltaTime, m_LastMousePositions, m_Layer);
		else if (!hovered)
		{
			windowState.dragActive = false;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				m_Layer.CommitViewInteraction(windowState.windowId);
			}
		}

		if (windowState.cursor3DPlaced && windowState.camera != nullptr)
		{
			const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
			const glm::vec4 clip = viewProjection * glm::vec4(windowState.cursor3DPosition, 1.0f);
			if (clip.w > 0.0001f)
			{
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				const ImVec2 screen(
					imageOrigin.x + (ndc.x * 0.5f + 0.5f) * windowState.viewportSize.x,
					imageOrigin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * windowState.viewportSize.y);
				ImDrawList *drawList = ImGui::GetWindowDrawList();
				constexpr float kCrossRadius = 9.0f;
				constexpr ImU32 kCursorColor = IM_COL32(255, 255, 255, 230);
				constexpr ImU32 kCursorOutline = IM_COL32(20, 20, 20, 200);
				drawList->AddCircle(screen, kCrossRadius, kCursorOutline, 0, 3.0f);
				drawList->AddCircle(screen, kCrossRadius, kCursorColor, 0, 1.5f);
				drawList->AddLine(
					ImVec2(screen.x - kCrossRadius - 4.0f, screen.y), ImVec2(screen.x + kCrossRadius + 4.0f, screen.y), kCursorColor, 1.5f);
				drawList->AddLine(
					ImVec2(screen.x, screen.y - kCrossRadius - 4.0f), ImVec2(screen.x, screen.y + kCrossRadius + 4.0f), kCursorColor, 1.5f);
			}
		}

		// Last, so the N panel floats over everything already drawn on the image.
		const ViewportSidePanelRect sidePanelRect = DrawViewportSidePanel(
			windowState, imageOrigin, viewportSize, deltaTime, m_Layer, m_CommandRegistry, m_DomainLayer);

		// The side panel is an overlay drawn after the image. Its grip used to reach the picker because
		// `hovered` was captured from the image before the overlay existed, so starting a resize could
		// clear the selection through HandleViewportPick's empty-space branch. Submit the overlay first,
		// then reject clicks whose press began on the panel or its six-pixel grip.
		const ImVec2 clickedAt = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
		const bool startedOnSidePanel = sidePanelRect.Contains(glm::vec2(clickedAt.x, clickedAt.y));

		if (gizmoCapturing || selectionToolConsumedMouse)
		{
			// Nothing else consumes mouse input this frame.
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::Cursor3D)
		{
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !startedOnSidePanel)
			{
				const ImVec2 mousePos = ImGui::GetMousePos();
				(void)handleCursor3DPlacement(windowState, mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);
			}
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::MeasureBond ||
			windowState.activeSelectionTool == SelectionToolMode::MeasureAngle)
		{
			if (!startedOnSidePanel)
				handleMeasureToolClick(windowState, imageOrigin, hovered);
		}
		else if (hovered && !startedOnSidePanel)
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
					HandleViewportPick(windowState, relX, relY, io.KeyCtrl, m_Layer);
				}
			}
		}

		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
	}

	// Vertical-toolbar Measure Bond/Angle tool: click accumulates atoms into the normal selection
	// (reusing HandleAtomPick's raycast and the existing additive-toggle semantics of
	// AtomSelectionRequested) until it reaches 2 (bond) or 3 (angle), fires the same bulk-pin event
	// the M/Shift+M keybinds use, then clears the selection so the next click starts a fresh pick -
	// the tool itself stays active (VESTA-style: keep measuring pairs without re-selecting the tool).
	void RendererPanel::handleMeasureToolClick(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered)
	{
		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			return;

		const ImVec2 mousePos = ImGui::GetMousePos();
		const float relX = mousePos.x - imageOrigin.x;
		const float relY = mousePos.y - imageOrigin.y;
		if (relX < 0.0f || relY < 0.0f || relX >= windowState.viewportSize.x || relY >= windowState.viewportSize.y)
			return;

		HandleAtomPick(
			windowState, relX, relY, /*additive=*/!windowState.selectedAtomIndices.empty(), m_Layer);

		const std::size_t required = windowState.activeSelectionTool == SelectionToolMode::MeasureBond ? 2 : 3;
		if (windowState.selectedAtomIndices.size() < required)
			return;

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return;

		if (windowState.activeSelectionTool == SelectionToolMode::MeasureBond)
		{
			RendererEvents::Viewport::LabelsToggleSelectedBondRequested pinEvent;
			pinEvent.windowId = windowState.windowId;
			eventBus->Publish(pinEvent);
		}
		else
		{
			RendererEvents::Viewport::LabelsToggleSelectedAngleRequested pinEvent;
			pinEvent.windowId = windowState.windowId;
			eventBus->Publish(pinEvent);
		}

		RendererEvents::Viewport::AtomSelectionRequested clearEvent;
		clearEvent.windowId = windowState.windowId;
		clearEvent.additive = false;
		eventBus->Publish(clearEvent);
	}

	// Ray-casts relX/relY (viewport-relative pixels) into the scene: snaps to the picked atom if the
	// click landed on one (same ray/pick-radius as HandleAtomPick), otherwise drops onto the plane
	// through the camera's orbit target, perpendicular to the view direction - a reasonable depth
	// for "wherever you clicked in empty space" without needing real scene-depth picking. Shared by
	// the 3D-cursor tool click and the viewport context menu's "Set 3D cursor here".
	glm::vec3 RendererPanel::computeViewportWorldPosition(const RendererWindowState &windowState, float relX, float relY) const
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

		float bestT = std::numeric_limits<float>::max();
		glm::vec3 hitPosition(0.0f);
		bool hitAtom = false;
		for (const RendererAtomData &atom : windowState.structure.atoms)
		{
			if (!atom.visible)
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
			if (t > 0.001f && t < bestT)
			{
				bestT = t;
				hitPosition = atom.cartesianPosition;
				hitAtom = true;
			}
		}

		if (!hitAtom)
		{
			const glm::vec3 forward = glm::normalize(windowState.camera->Target() - rayOrigin);
			const float denom = glm::dot(rayDir, forward);
			const float planeT = std::abs(denom) > 0.0001f ? glm::dot(windowState.camera->Target() - rayOrigin, forward) / denom : 0.0f;
			hitPosition = rayOrigin + rayDir * planeT;
		}
		return hitPosition;
	}

	// 3D cursor tool click - see computeViewportWorldPosition for the hit/plane logic.
	bool RendererPanel::handleCursor3DPlacement(RendererWindowState &windowState, float relX, float relY)
	{
		if (!windowState.camera)
			return false;

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return false;

		RendererEvents::Viewport::Cursor3DSetPositionRequested event;
		event.windowId = windowState.windowId;
		event.position = computeViewportWorldPosition(windowState, relX, relY);
		eventBus->Publish(event);
		return true;
	}

	// Blender-style "adjust last operation" panel for a just-added SceneArrow - set active by every
	// Add Arrow entry point (Shift+A menu, right-click Add submenu, ObjectPropertiesPanel's own
	// "+ Add arrow" button). Anchored to THIS window's own viewport image (not the whole app), bottom
	// -left, so it reads as belonging to the arrow just added here. Closes itself - no explicit close
	// button needed beyond "Done" - the moment selection moves away from the arrow it was opened for
	// (Escape, clicking something else, deleting it), since at that point selectedSceneArrows no
	// longer matches sceneArrowQuickEditIndex exactly.
	void RendererPanel::renderSceneArrowQuickEditPanel(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (!windowState.sceneArrowQuickEditActive)
			return;
		if (windowState.sceneArrowQuickEditIndex >= windowState.sceneArrows.size() ||
			windowState.selectedSceneArrows.size() != 1 ||
			ArrowIndex(windowState, windowState.selectedSceneArrows[0]) != windowState.sceneArrowQuickEditIndex)
		{
			windowState.sceneArrowQuickEditActive = false;
			return;
		}

		ImGui::SetNextWindowPos(
			ImVec2(imageOrigin.x + 12.0f, imageOrigin.y + imageSize.y - 12.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
		constexpr ImGuiWindowFlags kFlags =
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
		// "###..." + windowId keeps this popup's ImGui identity distinct per structure window - same
		// reason renderStructureWindow's own imguiWindowLabel does, otherwise two windows with an
		// active quick-edit at once would collide onto the same popup.
		const std::string popupLabel = "Add Arrow###SceneArrowQuickEdit_" + windowState.windowId;
		if (ImGui::Begin(popupLabel.c_str(), nullptr, kFlags))
		{
			DrawSceneArrowEditor(
				windowState, windowState.sceneArrowQuickEditIndex, SceneArrowEditorMode::Compact,
				m_Layer.GetGlobalSettings());
			if (ImGui::Button("Done"))
				windowState.sceneArrowQuickEditActive = false;
		}
		ImGui::End();
	}

	// Right-click viewport context menu. Delete/Hide/Duplicate/Copy/Paste/Select All route through
	// CommandRegistry using the SAME command IDs their keybindings use (identical behaviour, undo
	// history stays consistent); Clear Selection and the 3D-cursor items are cheap enough to publish
	// directly, matching the rest of this panel's style for non-domain, non-undoable state.
	void RendererPanel::renderViewportContextMenu(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		(void)hovered;

		// Opened by hand instead of with BeginPopupContextItem. That helper hangs off whichever item
		// was submitted last, and by the time this runs the drag-drop target, the gizmo chain and the
		// pin/label/arrow/orbital handlers have all had their chance to submit items of their own -
		// so the menu attached itself to one of those and stopped opening over the viewport at all.
		// The image rect does not move, so the hit test uses that.
		constexpr const char *kContextMenuId = "##RendererViewportContextMenu";
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool overImage = mouse.x >= imageOrigin.x && mouse.y >= imageOrigin.y &&
			mouse.x < imageOrigin.x + imageSize.x && mouse.y < imageOrigin.y + imageSize.y;
		// Released, not clicked, and only when the button barely moved - a right-drag is a camera
		// move (ApplyViewportInputNavigation), and it must not end in a popup.
		const bool rightDragged = ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] > 25.0f;
		if (overImage && ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			!rightDragged && !ImGui::IsPopupOpen(kContextMenuId))
			ImGui::OpenPopup(kContextMenuId);

		if (!ImGui::BeginPopup(kContextMenuId))
			return;

		// Recorded here, on the frame the popup actually appears, rather than in a separate
		// "hovered && right-clicked" branch outside it. Those were two different conditions deciding
		// one event, and whenever they disagreed - a gizmo or overlay under the cursor eating the
		// panel's hover flag while ImGui still opened the menu - this field silently kept its
		// initial value and every object added from the menu (Atom, Label, Arrow, Orbital) landed at
		// the world origin, off-screen for anything but a structure sitting at 0,0,0.
		// GetMousePosOnOpeningCurrentPopup is ImGui's own record of where the popup was opened, so
		// the two can no longer drift apart.
		if (ImGui::IsWindowAppearing())
		{
			const ImVec2 openedAt = ImGui::GetMousePosOnOpeningCurrentPopup();
			m_ContextMenuWorldPosition =
				computeViewportWorldPosition(windowState, openedAt.x - imageOrigin.x, openedAt.y - imageOrigin.y);
		}

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
		const bool hasSelection = !windowState.selectedAtomIndices.empty();

		auto runCommand = [&](const char *commandId)
		{
			if (commandRegistry == nullptr)
				return;
			Result<CommandOutcome> result = commandRegistry->Execute(CommandID{commandId}, {});
			if (!result)
				DS_LOG_WARN("Viewport context menu command '{}' failed: {}", commandId, result.Error().technicalDetails);
		};

		if (ImGui::BeginMenu("Add"))
		{
			// Reuses the same Add Atom popup Shift+A opens (drawAddAtomPopup) rather than a separate
			// flow - mirrors the flag-setting Render() already does when addAtomPopupRequested comes
			// in via that event, just seeded with this menu's own click position instead of the 3D
			// cursor/origin default.
			if (ImGui::MenuItem("Atom..."))
			{
				m_AddAtomPopupRequested = true;
				m_AddAtomPopupWindowId = windowState.windowId;
				m_AddAtomPopupPosition = m_ContextMenuWorldPosition;
				m_AddAtomPopupFractional = false;
			}
			if (ImGui::MenuItem("Label"))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				RendererWindowState::FreeLabel label;
				label.id = windowState.sceneRegistry.AllocateObjectId();
				label.worldPosition = m_ContextMenuWorldPosition;
				windowState.freeLabels.push_back(std::move(label));
			}
			DrawFreeSegmentAddItems(windowState, m_ContextMenuWorldPosition);
			DrawFreePlaneAddItem(windowState, m_ContextMenuWorldPosition);
			const DrawSelectionDescription drawSelection = DescribeDrawSelection(windowState);
			if (ImGui::BeginMenu(drawSelection.menuLabel.c_str()))
			{
				DrawSegmentAddItems(windowState);
				DrawPlaneAddItem(windowState);
				ImGui::EndMenu();
			}
			DrawOrbitalAddMenu(windowState, m_ContextMenuWorldPosition);
			DrawScenePathDevAddMenu(windowState, m_ContextMenuWorldPosition);
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
			runCommand("renderer.selection.copy");
		if (ImGui::MenuItem("Paste", "Ctrl+V"))
			runCommand("renderer.selection.paste");
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
			runCommand("renderer.selection.duplicate");

		// Copies from the first selected arrow (same "first selected wins" convention the 3D Cursor
		// submenu below already uses); pastes onto every selected arrow as one undo step. Two independent
		// clipboards (GetArrowGeometryClipboard/GetArrowStyleClipboard) rather than one tagged slot, so
		// Paste Geometry/Style are only enabled once that specific thing has actually been copied.
		const bool hasArrowSelection = !windowState.selectedSceneArrows.empty();
		if (ImGui::BeginMenu("Arrow", hasArrowSelection || GetArrowGeometryClipboard().has_value() ||
										   GetArrowStyleClipboard().has_value()))
		{
			if (ImGui::MenuItem("Copy Geometry", nullptr, false, hasArrowSelection))
				CopyArrowGeometry(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].style);
			if (ImGui::MenuItem("Copy Style", nullptr, false, hasArrowSelection))
				CopyArrowStyle(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].style);
			if (ImGui::MenuItem("Copy Geometry + Style", nullptr, false, hasArrowSelection))
			{
				const RendererWindowState::ArrowStyle &style =
					windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].style;
				CopyArrowGeometry(style);
				CopyArrowStyle(style);
			}

			ImGui::Separator();

			const bool canPasteGeometry = hasArrowSelection && GetArrowGeometryClipboard().has_value();
			const bool canPasteStyle = hasArrowSelection && GetArrowStyleClipboard().has_value();
			if (ImGui::MenuItem("Paste Geometry", nullptr, false, canPasteGeometry))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteArrowGeometry(windowState, windowState.selectedSceneArrows);
			}
			if (ImGui::MenuItem("Paste Style", nullptr, false, canPasteStyle))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteArrowStyle(windowState, windowState.selectedSceneArrows);
			}
			if (ImGui::MenuItem("Paste Geometry + Style", nullptr, false, canPasteGeometry && canPasteStyle))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteArrowGeometry(windowState, windowState.selectedSceneArrows);
				PasteArrowStyle(windowState, windowState.selectedSceneArrows);
			}

			ImGui::EndMenu();
		}

		// notes.txt pt. 15 - mirrors the "Arrow" submenu above, but LabelStyle has no separate
		// geometry to split out, so just one Copy/Paste Style pair. Applies to whichever label kind is
		// selected (pinned bond/angle labels and free labels share this one clipboard, same as the
		// "Selected labels" bulk editor in ObjectPropertiesPanel).
		const bool hasLabelSelection =
			!windowState.selectedPinnedMeasurements.empty() || !windowState.selectedFreeLabels.empty();
		if (ImGui::BeginMenu("Label", hasLabelSelection || GetLabelStyleClipboard().has_value()))
		{
			if (ImGui::MenuItem("Copy Style", nullptr, false, hasLabelSelection))
			{
				const RendererWindowState::LabelStyle *style = nullptr;
				if (!windowState.selectedPinnedMeasurements.empty())
				{
					if (const auto *pin = FindAnnotation(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements.front()))
						style = &pin->style;
				}
				else if (const auto *label = FindAnnotation(windowState.freeLabels, windowState.selectedFreeLabels.front()))
				{
					style = &label->style;
				}
				if (style != nullptr)
					CopyLabelStyle(*style);
			}
			const bool canPasteLabelStyle = hasLabelSelection && GetLabelStyleClipboard().has_value();
			if (ImGui::MenuItem("Paste Style", nullptr, false, canPasteLabelStyle))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				PasteLabelStyle(windowState, windowState.selectedPinnedMeasurements, windowState.selectedFreeLabels);
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
			runCommand("renderer.selection.delete");
		// H hides every selected kind, not only atoms - see Renderer/Scene/SceneVisibility.hpp.
		if (ImGui::MenuItem("Hide", "H", false, AnySceneObjectSelected(windowState)))
			runCommand("renderer.selection.hide");

		if (ImGui::BeginMenu("Change type", hasSelection))
		{
			static char speciesBuffer[8] = "";
			ImGui::SetNextItemWidth(80.0f);
			const bool enterPressed = ImGui::InputText(
				"##ChangeTypeInput", speciesBuffer, sizeof(speciesBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			const bool applyPressed = ImGui::SmallButton("Apply");
			if ((enterPressed || applyPressed) && speciesBuffer[0] != '\0' && commandRegistry != nullptr)
			{
				ChangeAtomTypePayload payload;
				payload.windowId = windowState.windowId;
				payload.species = speciesBuffer;
				CommandContext context;
				context.Set<ChangeAtomTypePayload>("atom_edit.change_type_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.selection.change_type"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Change atom type failed: {}", result.Error().technicalDetails);
				speciesBuffer[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Select All", "Ctrl+A"))
			runCommand("renderer.selection.select_all");
		if (ImGui::MenuItem("Clear Selection", nullptr, false, hasSelection) && eventBus != nullptr)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.additive = false;
			eventBus->Publish(event);
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("3D Cursor"))
		{
			auto publishCursor = [&](const glm::vec3 &position)
			{
				if (eventBus == nullptr)
					return;
				RendererEvents::Viewport::Cursor3DSetPositionRequested event;
				event.windowId = windowState.windowId;
				event.position = position;
				eventBus->Publish(event);
			};

			if (ImGui::MenuItem("Set Here"))
				publishCursor(m_ContextMenuWorldPosition);

			if (ImGui::MenuItem("Move to Selection Center", nullptr, false, hasSelection))
			{
				glm::vec3 centroid(0.0f);
				for (const std::size_t atomIndex : windowState.selectedAtomIndices)
					centroid += windowState.structure.atoms[atomIndex].cartesianPosition;
				centroid /= static_cast<float>(windowState.selectedAtomIndices.size());
				publishCursor(centroid);
			}
			if (ImGui::MenuItem("Move to First Selected", nullptr, false, hasSelection))
				publishCursor(windowState.structure.atoms[windowState.selectedAtomIndices.front()].cartesianPosition);
			if (ImGui::MenuItem("Move to Last Selected", nullptr, false, hasSelection))
				publishCursor(windowState.structure.atoms[windowState.selectedAtomIndices.back()].cartesianPosition);
			if (ImGui::MenuItem("Move to Origin"))
				publishCursor(glm::vec3(0.0f));

			const bool hasOneArrowSelected = windowState.selectedSceneArrows.size() == 1;
			if (ImGui::MenuItem("Move to Arrow Start", nullptr, false, hasOneArrowSelected))
				publishCursor(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].start());
			if (ImGui::MenuItem("Move to Arrow End", nullptr, false, hasOneArrowSelected))
				publishCursor(windowState.sceneArrows[ArrowIndex(windowState, windowState.selectedSceneArrows.front())].end());

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}

	void RendererPanel::drawPeriodicTableWindow()
	{
		if (!m_Layer.GetShowPeriodicTableWindow())
			return;

		ImGui::SetNextWindowSize(ImVec2(1260.0f, 640.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Periodic Table", &m_Layer.GetShowPeriodicTableWindow()))
		{
			ImGui::End();
			return;
		}

		const std::string &focusedWindowId = m_Layer.GetFocusedViewportWindowId();
		const RendererWindowState *focusedWindow = nullptr;
		for (const RendererWindowState &candidate : m_Layer.GetWindows())
		{
			if (candidate.windowId == focusedWindowId)
			{
				focusedWindow = &candidate;
				break;
			}
		}
		const bool canApply = focusedWindow != nullptr && !focusedWindow->selectedAtomIndices.empty();

		auto applyToSelectedAtoms = [&]()
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry == nullptr)
				return;
			ChangeAtomTypePayload payload;
			payload.windowId = focusedWindowId;
			payload.species = m_Layer.GetSelectedPeriodicElement();
			CommandContext context;
			context.Set<ChangeAtomTypePayload>("atom_edit.change_type_payload", std::move(payload));
			Result<CommandOutcome> result =
				commandRegistry->Execute(CommandID{"renderer.selection.change_type"}, std::move(context));
			if (!result)
				DS_LOG_WARN("Change atom type from periodic table failed: {}", result.Error().technicalDetails);
		};

		// Was a hand-rolled duplicate of DrawPeriodicTableGrid (plain gray buttons, small fixed cell
		// size, no per-category color, no readable-text contrast fix) - reuses the shared, colored,
		// already-fixed-up grid instead, same as ElementCatalogPanel, plus a bigger font scale so the
		// larger cells below aren't mostly empty padding around a tiny symbol.
		ImGui::SetWindowFontScale(1.2f);
		const ImVec2 cellSize(54.0f, 46.0f);
		std::string doubleClicked;
		const std::string clicked = DrawPeriodicTableGrid(
			m_Layer,
			[&](const std::string &symbol) -> glm::vec3
			{ return CategoryColor(ClassifyElement(AtomicNumberForSymbol(m_Layer, symbol))); },
			m_Layer.GetSelectedPeriodicElement(), cellSize, &doubleClicked);
		ImGui::SetWindowFontScale(1.0f);
		if (!clicked.empty())
			m_Layer.GetSelectedPeriodicElement() = clicked;

		// Confirming a pick - double-click on a cell, or Enter once one is selected - closes the
		// window like a normal quick-pick popup. GetPeriodicTableApplyOnConfirm() distinguishes WHY
		// this window is open: opened from Object Properties' "Choose..." (changing an EXISTING
		// selection's element), confirming should also apply it - the window is about to disappear,
		// so there's no later chance to press the "Apply" button below. Opened from Add Atom's
		// "Choose..." (picking a species for a NOT-YET-inserted atom), confirming should just close -
		// Add Atom reads the selected symbol itself when its own Insert button runs, and unrelated
		// atoms possibly selected in the viewport at the same time must NOT be silently retyped.
		const bool confirmedViaEnter = !m_Layer.GetSelectedPeriodicElement().empty() &&
			(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
		if (!doubleClicked.empty() || confirmedViaEnter)
		{
			if (m_Layer.GetPeriodicTableApplyOnConfirm() && canApply)
				applyToSelectedAtoms();
			m_Layer.GetShowPeriodicTableWindow() = false;
		}

		ImGui::Separator();
		ImGui::Text("Selected element: %s", m_Layer.GetSelectedPeriodicElement().c_str());

		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Apply to selected atoms"))
			applyToSelectedAtoms();
		ImGui::EndDisabled();
		if (!canApply && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Select atoms in a renderer viewport first.");

		ImGui::End();
	}

} // namespace DefectStudio
