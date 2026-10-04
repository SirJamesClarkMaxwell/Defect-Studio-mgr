#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportInput.hpp"
#include "Presentation/Panels/ViewportInteraction.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"
#include "Presentation/Panels/ViewportPathOverlay.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportTextEditor.hpp"

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
#include "Core/Input/ContextManager.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Domain/Defects/DefectModel.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathEditCommands.hpp"
#include "Presentation/Panels/ViewportSidePanel.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneVisibility.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
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
		{
			RegisterViewportSceneObjectCommands(*registry, m_Layer);
			RegisterScenePathEditCommands(*registry, m_Layer);
		}
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
		if (Ref<ContextManager> contexts = m_ContextManager.lock())
			UpdateScenePathEditContext(m_Layer, *contexts);
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
		if (windowState.isProjectScene && windowState.sceneObjectsDirty)
			displayTitle += "*";

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
		windowState.labelPickQuads = m_Layer.GetLabelPickQuads(windowState.windowId);

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
		DrawViewportDefectFrameOverlay(windowState, imageOrigin, viewportSize);
		DrawSelectedVacancyOverlay(windowState, imageOrigin, viewportSize);
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
		// The keymap may already have left Edit Mode before rendering; its previous-frame context
		// still owns that Escape until the context update at the end of this frame.
		const Ref<ContextManager> contexts = m_ContextManager.lock();
		if (hovered && !IsViewportTextEditorActive(windowState) && !ImGui::GetIO().WantTextInput && !windowState.pathEdit.IsActive() && (!contexts || !contexts->IsActive(kPathEditActiveContext)) &&
			!windowState.modalTransform.has_value() && !windowState.pinnedMeasurementDragging &&
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

		if (!IsViewportTextEditorActive(windowState) && !ImGui::GetIO().WantTextInput)
			HandleVacancyKeyboardShortcuts(
				windowState, hovered, m_Layer, m_DomainLayer.lock().get(), m_CommandRegistry.lock().get());
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
		else if (windowState.activeSelectionTool == SelectionToolMode::Text)
		{
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !startedOnSidePanel && !ImGui::GetIO().KeyAlt)
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				HandleViewportTextToolClick(windowState, imageOrigin, viewportSize,
					computeViewportWorldPosition(windowState, mouse.x - imageOrigin.x, mouse.y - imageOrigin.y));
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

		DrawViewportTextEditor(windowState, imageOrigin, viewportSize);
		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
	}

} // namespace DefectStudio
