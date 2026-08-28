#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
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
#include "Renderer/Scene/SceneSystem.hpp"
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

		// Parses a Blender-style typed numeric override ("-3.5" while it's still being typed, possibly
		// just "-" or "." mid-entry) - unparsable/partial input reads as 0 rather than erroring, since
		// the caller applies this every frame while the buffer is non-empty.
		[[nodiscard]] float ParseTypedNumber(const std::string &buffer)
		{
			float value = 0.0f;
			std::from_chars(buffer.data(), buffer.data() + buffer.size(), value);
			return value;
		}

		// Captures Blender-style numeric-override keystrokes (digits, sign, decimal point, Backspace)
		// into windowState.fallbackNumericInput while a locked-axis fallback drag is active - shared by
		// both the rotate axis-locked path and the translate/scale path below since the key handling is
		// identical, only what the resulting number MEANS differs per call site.
		void CaptureGizmoNumericInput(RendererWindowState &windowState)
		{
			for (int digit = 0; digit < 10; ++digit)
			{
				if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit), false) ||
					ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit), false))
					windowState.fallbackNumericInput += static_cast<char>('0' + digit);
			}
			if ((ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) &&
				windowState.fallbackNumericInput.find('-') == std::string::npos)
				windowState.fallbackNumericInput.insert(windowState.fallbackNumericInput.begin(), '-');
			if ((ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) &&
				windowState.fallbackNumericInput.find('.') == std::string::npos)
				windowState.fallbackNumericInput += '.';
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !windowState.fallbackNumericInput.empty())
				windowState.fallbackNumericInput.pop_back();
		}

		// Resolves the same anchor point renderLabels() draws a pin at (bond midpoint / angle vertex),
		// worldOffset already applied - ignores the bond's periodic-image shift for a 2-atom pin (only
		// matters for bonds crossing a periodic cell boundary), fine for a hit-test. Shared by the
		// region-select (box/circle) hit-testers below; handlePinnedMeasurementInteraction keeps its
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

		// Mean of a frozen position snapshot - used by the numeric-override rotate/scale paths below
		// to get a pivot that stays fixed for the whole typed-number entry instead of the live
		// per-frame selection centroid (see call sites: mixing a frozen snapshot with a pivot that is
		// itself recomputed from the snapshot's own output each frame is a feedback loop, and for
		// rotation in particular it's a DIVERGING one - the per-frame pivot error e satisfies
		// e(t+1) = (I-R)*e(t), and |I-R| = 2*sin(angle/2) exceeds 1 for any angle over 60 degrees, so
		// a typed 90 degree rotation doubled its own error roughly every frame and threw the selection
		// off-screen within a few dozen frames).
		[[nodiscard]] glm::vec3 MeanPosition(const std::vector<glm::vec3> &positions)
		{
			if (positions.empty())
				return glm::vec3(0.0f);
			glm::vec3 sum(0.0f);
			for (const glm::vec3 &position : positions)
				sum += position;
			return sum / static_cast<float>(positions.size());
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

		drawPeriodicTableWindow();
		drawAddMenu();
		drawAddAtomPopup();
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

		drawViewportVerticalToolbar(windowState);
		ImGui::SameLine();

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

		// Escape always deselects, regardless of how a click landed you in this state - a reliable
		// way out when the gizmo's screen-space pick band swallows a click meant to clear selection
		// (the gizmo disappears once nothing is selected, since renderTransformGizmo() early-returns
		// with an empty selection). Doesn't try to cancel/revert a drag already in progress - only
		// acts when nothing is actively being dragged, so it can't leave a transform half-applied.
		if (hovered && !windowState.fallbackGizmoDragging && !windowState.pinnedMeasurementDragging &&
			!windowState.freeLabelDragging && !windowState.sceneArrowDragging && !windowState.sceneArrowGizmoDragging &&
			!windowState.selectionDragActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedSceneArrows.clear();
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

		// renderTransformGizmo() returns whether it's hovered/dragging using OUR OWN screen-space
		// hit-test, not ImGuizmo::IsOver()/IsUsing() - those proved unreliable in both directions
		// (see the big comment inside renderTransformGizmo): sometimes falsely true, which blocked
		// our fallback pick from ever starting so the click did nothing; sometimes falsely false
		// right as a fallback drag begins, letting handleAtomPick fire on the same frame and
		// silently re-pick whichever atom is nearest the cursor (off in space along the arrow, not
		// the original selection) - selection jumping mid-drag. A live gizmo drag/hover takes
		// exclusive control of the viewport for the frame - suppress box/circle-select and
		// atom-pick so grabbing a handle doesn't also fire a click-select underneath it.
		// Keeps each label entity's TransformComponent current before the gizmo/hit-test below read
		// it - anchors move every frame with the atoms they measure (gizmo drag, nudge, relaxation
		// playback), so a stale transform would visibly lag a frame behind the label's own draw.
		SceneSystem::UpdateLabelTransforms(windowState.sceneRegistry, windowState);

		// Keyboard-only pin shortcuts (F flip / Delete / Ctrl+Shift+</>) run unconditionally - they
		// have no mouse hit-test of their own, so short-circuiting them behind an earlier gizmo's
		// mouse-capture (below) would silently drop them whenever the mouse happens to be hovering
		// that gizmo's pick band.
		handlePinnedMeasurementKeyboardShortcuts(windowState, hovered);

		// Short-circuiting `||` is intentional here (unlike the keyboard call above): each function's
		// mouse click/drag-start logic must NOT also run once an earlier one already claimed this
		// frame's click - e.g. clicking an atom gizmo handle must not also be reinterpreted as a pin
		// pick by handlePinnedMeasurementInteraction's own hit-test underneath it.
		const bool gizmoCapturing = renderTransformGizmo(windowState, imageOrigin, viewportSize, hovered) ||
			renderLabelTransformGizmo(windowState, imageOrigin, viewportSize, hovered) ||
			renderSceneArrowTransformGizmo(windowState, imageOrigin, viewportSize, hovered) ||
			handlePinnedMeasurementInteraction(windowState, imageOrigin, viewportSize, hovered) ||
			handleFreeLabelInteraction(windowState, imageOrigin, viewportSize, hovered) ||
			handleSceneArrowInteraction(windowState, imageOrigin, viewportSize, hovered);

		// Shift+A (RendererEvents::Viewport::AddAtomPopupToggleRequested) can only flag intent on
		// RendererWindowState - it has no access to this panel's own popup-request members, so it's
		// picked up and forwarded here, once per frame. Opens the Blender-style "what to add" menu
		// (drawAddMenu) rather than jumping straight to the Add Atom dialog - Atom is one choice
		// among others (Label) now, not the only thing Shift+A can mean.
		if (windowState.addAtomPopupRequested)
		{
			windowState.addAtomPopupRequested = false;
			m_AddMenuRequested = true;
			m_AddMenuWindowId = windowState.windowId;
			m_AddMenuScreenPos = ImGui::GetMousePos();
			// Defaults: if a 3D cursor is already placed, start from there (Cartesian, since the
			// cursor's own position is stored Cartesian) - otherwise Fractional is a more useful
			// starting point than Cartesian (0,0,0) is for an ATOM (fractional (0,0,0) is a real cell
			// corner, Cartesian (0,0,0) is often nowhere near the visible structure); a Label reuses
			// this same vector as a plain Cartesian position either way, see drawAddMenu.
			if (windowState.cursor3DPlaced)
			{
				m_AddMenuPosition = windowState.cursor3DPosition;
				m_AddMenuPositionFractional = false;
			}
			else
			{
				m_AddMenuPosition = glm::vec3(0.0f);
				m_AddMenuPositionFractional = true;
			}
		}

		renderViewportContextMenu(windowState, imageOrigin, viewportSize, hovered);
		renderSceneArrowQuickEditPanel(windowState, imageOrigin, viewportSize);

		// Small handle dots at the start/end of every selected SceneArrow - not a transform gizmo,
		// just a visible answer to "where exactly is the end I can drag" (selection itself had no
		// visual feedback at all before this - same orange accent as box/circle-select below and as
		// atom/bond selection highlighting). The endpoint currently targeted by an active single-arrow
		// drag draws larger so a drag in progress is unambiguous too.
		if (windowState.camera != nullptr && !windowState.selectedSceneArrows.empty())
		{
			const glm::mat4 handleViewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
			auto projectHandle = [&](const glm::vec3 &world, ImVec2 &outScreen) -> bool {
				const glm::vec4 clip = handleViewProjection * glm::vec4(world, 1.0f);
				if (clip.w <= 0.0001f)
					return false;
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				outScreen = ImVec2(
					imageOrigin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
					imageOrigin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y);
				return true;
			};
			ImDrawList *handleDrawList = ImGui::GetWindowDrawList();
			constexpr float kHandleRadius = 5.0f;
			constexpr float kActiveHandleRadius = 7.0f;
			const bool singleDragging = windowState.sceneArrowDragging && windowState.selectedSceneArrows.size() == 1;
			using DragTarget = RendererWindowState::SceneArrowDragTarget;
			for (const std::size_t arrowIndex : windowState.selectedSceneArrows)
			{
				if (arrowIndex >= windowState.sceneArrows.size())
					continue;
				const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
				ImVec2 startScreen, endScreen;
				if (projectHandle(arrow.start, startScreen))
				{
					const bool active = singleDragging &&
						(windowState.sceneArrowDragTarget == DragTarget::Start || windowState.sceneArrowDragTarget == DragTarget::Both);
					const float radius = active ? kActiveHandleRadius : kHandleRadius;
					handleDrawList->AddCircleFilled(startScreen, radius, IM_COL32(255, 200, 60, 220));
					handleDrawList->AddCircle(startScreen, radius, IM_COL32(40, 25, 0, 255), 0, 1.5f);
				}
				if (projectHandle(arrow.end, endScreen))
				{
					const bool active = singleDragging &&
						(windowState.sceneArrowDragTarget == DragTarget::End || windowState.sceneArrowDragTarget == DragTarget::Both);
					const float radius = active ? kActiveHandleRadius : kHandleRadius;
					handleDrawList->AddCircleFilled(endScreen, radius, IM_COL32(255, 200, 60, 220));
					handleDrawList->AddCircle(endScreen, radius, IM_COL32(40, 25, 0, 255), 0, 1.5f);
				}
			}
		}

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

		applyContinuousNudge(windowState, deltaTime);
		applyContinuousPan(windowState, deltaTime);

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
		else if (windowState.activeSelectionTool == SelectionToolMode::Cursor3D)
		{
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImVec2 mousePos = ImGui::GetMousePos();
				(void)handleCursor3DPlacement(windowState, mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);
			}
		}
		else if (windowState.activeSelectionTool == SelectionToolMode::MeasureBond ||
			windowState.activeSelectionTool == SelectionToolMode::MeasureAngle)
		{
			handleMeasureToolClick(windowState, imageOrigin, hovered);
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
					handleViewportPick(windowState, relX, relY, io.KeyCtrl);
				}
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

		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
	}

	void RendererPanel::handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive)
	{
		if (!windowState.pickAtoms)
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

		float bestT = std::numeric_limits<float>::max();
		std::size_t hitIndex = std::numeric_limits<std::size_t>::max();

		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			const RendererAtomData &atom = windowState.structure.atoms[i];
			if (!atom.visible)
				continue;
			const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
			const float a = glm::dot(rayDir, rayDir);
			const float b = 2.0f * glm::dot(oc, rayDir);
			// Padded ~35% past the visible sphere - clicking exactly on a rendered edge (anti-
			// aliasing, small atoms like H) otherwise misses more often than it should.
			const float pickRadius = atom.radius * 1.35f;
			const float c = glm::dot(oc, oc) - pickRadius * pickRadius;
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

	void RendererPanel::handleViewportPick(RendererWindowState &windowState, float relX, float relY, bool additive)
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
		if (windowState.pickAtoms)
		{
			for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
			{
				const RendererAtomData &atom = windowState.structure.atoms[i];
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
		if (atomHit && (!bondHit || bestAtomT <= bestBondT))
		{
			handleAtomPick(windowState, relX, relY, additive);
			return;
		}

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
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

	// Vertical-toolbar Measure Bond/Angle tool: click accumulates atoms into the normal selection
	// (reusing handleAtomPick's raycast and the existing additive-toggle semantics of
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

		handleAtomPick(windowState, relX, relY, /*additive=*/!windowState.selectedAtomIndices.empty());

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
	// click landed on one (same ray/pick-radius as handleAtomPick), otherwise drops onto the plane
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

	// Whether ANY atom's sphere is under screenPos (same ray/pick-radius as handleAtomPick, read-only
	// - no selection change). Used to give plain atom-click priority over the gizmo's axis pick band:
	// in a crystal lattice, a bonded neighbour very often sits almost exactly along a world axis from
	// the selected atom, right where the gizmo's own pick band lives - without this check, clicking
	// that neighbour to extend the selection (e.g. to build a 2-atom bond-length measurement) grabs
	// the gizmo instead of selecting it.
	bool RendererPanel::isAtomUnderScreenPosition(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos) const
	{
		if (!windowState.camera || windowState.viewportSize.x <= 0.0f || windowState.viewportSize.y <= 0.0f)
			return false;

		const float relX = screenPos.x - imageOrigin.x;
		const float relY = screenPos.y - imageOrigin.y;
		if (relX < 0.0f || relY < 0.0f || relX >= windowState.viewportSize.x || relY >= windowState.viewportSize.y)
			return false;

		const float ndcX = (2.0f * relX / windowState.viewportSize.x) - 1.0f;
		const float ndcY = -((2.0f * relY / windowState.viewportSize.y) - 1.0f);

		const glm::mat4 invVP = glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

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
			if (t > 0.001f)
				return true;
		}
		return false;
	}

	// Same idea as isAtomUnderScreenPosition, for bonds - a selected atom's own gizmo axis pick band
	// (kPickMaxDistance in renderTransformGizmo) very often overlaps a bonded neighbour's bond line
	// too, not just the neighbour atom itself, so this needs the same priority override to keep a
	// deliberate bond click from being swallowed by the gizmo.
	bool RendererPanel::isBondUnderScreenPosition(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos) const
	{
		if (!windowState.camera || windowState.viewportSize.x <= 0.0f || windowState.viewportSize.y <= 0.0f)
			return false;

		const float relX = screenPos.x - imageOrigin.x;
		const float relY = screenPos.y - imageOrigin.y;
		if (relX < 0.0f || relY < 0.0f || relX >= windowState.viewportSize.x || relY >= windowState.viewportSize.y)
			return false;

		const float ndcX = (2.0f * relX / windowState.viewportSize.x) - 1.0f;
		const float ndcY = -((2.0f * relY / windowState.viewportSize.y) - 1.0f);

		const glm::mat4 invVP = glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

		for (const RendererBondData &bond : windowState.structure.bonds)
		{
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
				rayOrigin, rayDir, firstAtom.cartesianPosition, secondAtom.cartesianPosition + bond.secondAtomPeriodicOffset,
				t, closestOnSegment);
			if (t <= 0.001f)
				continue;

			const float pickRadius = std::max(bond.radius * 2.5f, 0.12f);
			const glm::vec3 closestOnRay = rayOrigin + rayDir * t;
			if (glm::distance(closestOnRay, closestOnSegment) <= pickRadius)
				return true;
		}
		return false;
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
			windowState.selectedSceneArrows[0] != windowState.sceneArrowQuickEditIndex)
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
		(void)imageSize;
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			const ImVec2 mousePos = ImGui::GetMousePos();
			m_ContextMenuWorldPosition =
				computeViewportWorldPosition(windowState, mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);
		}

		if (!ImGui::BeginPopupContextItem("##RendererViewportContextMenu"))
			return;

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
				label.worldPosition = m_ContextMenuWorldPosition;
				windowState.freeLabels.push_back(std::move(label));
			}
			if (ImGui::MenuItem("Arrow"))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				windowState.sceneArrows.push_back(MakeDefaultSceneArrow(windowState, m_ContextMenuWorldPosition));
				const std::size_t newIndex = windowState.sceneArrows.size() - 1;
				windowState.selectedSceneArrows = {newIndex};
				windowState.sceneArrowQuickEditActive = true;
				windowState.sceneArrowQuickEditIndex = newIndex;
			}
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
				CopyArrowGeometry(windowState.sceneArrows[windowState.selectedSceneArrows.front()].style);
			if (ImGui::MenuItem("Copy Style", nullptr, false, hasArrowSelection))
				CopyArrowStyle(windowState.sceneArrows[windowState.selectedSceneArrows.front()].style);
			if (ImGui::MenuItem("Copy Geometry + Style", nullptr, false, hasArrowSelection))
			{
				const RendererWindowState::ArrowStyle &style =
					windowState.sceneArrows[windowState.selectedSceneArrows.front()].style;
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

		ImGui::Separator();

		if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
			runCommand("renderer.selection.delete");
		if (ImGui::MenuItem("Hide", "H", false, hasSelection))
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
				publishCursor(windowState.sceneArrows[windowState.selectedSceneArrows.front()].start);
			if (ImGui::MenuItem("Move to Arrow End", nullptr, false, hasOneArrowSelected))
				publishCursor(windowState.sceneArrows[windowState.selectedSceneArrows.front()].end);

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
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
			const RendererEvents::Viewport::RegionSelectMode mode = resolveRegionSelectMode(io.KeyShift, io.KeyCtrl);
			// Gated on pickAtoms/pickBonds the same way handleViewportPick's plain click already is -
			// box-select previously always matched atoms regardless of the active selection mode, and
			// never matched bonds at all even when the mode allowed picking them.
			publishRegionSelection(
				windowState,
				windowState.pickAtoms ? hitTestRect(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				windowState.pickBonds ? hitTestRectBonds(windowState, rectMin, rectMax) : std::vector<std::size_t>{},
				mode);
			// Labels aren't part of the atom/bond region-select event above (single-select fields, not
			// an entity list) - applied directly here instead, same pickLabels gate as everywhere else.
			if (windowState.pickLabels)
			{
				applyLabelRegionSelection(
					windowState, hitTestRectPinnedMeasurements(windowState, rectMin, rectMax),
					hitTestRectFreeLabels(windowState, rectMin, rectMax),
					hitTestRectSceneArrows(windowState, rectMin, rectMax), mode);
			}
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
		const RendererEvents::Viewport::RegionSelectMode mode = io.KeyShift
			? RendererEvents::Viewport::RegionSelectMode::Subtract
			: RendererEvents::Viewport::RegionSelectMode::Add;

		publishRegionSelection(
			windowState,
			windowState.pickAtoms ? hitTestCircle(windowState, center, windowState.circleSelectRadius)
								   : std::vector<std::size_t>{},
			windowState.pickBonds ? hitTestCircleBonds(windowState, center, windowState.circleSelectRadius)
								   : std::vector<std::size_t>{},
			mode);
		if (windowState.pickLabels)
		{
			applyLabelRegionSelection(
				windowState, hitTestCirclePinnedMeasurements(windowState, center, windowState.circleSelectRadius),
				hitTestCircleFreeLabels(windowState, center, windowState.circleSelectRadius),
				hitTestCircleSceneArrows(windowState, center, windowState.circleSelectRadius), mode);
		}
	}

	// G/R/S transform gizmo for the current selection. Pivot is the live centroid of selected
	// atoms, recomputed every frame (not cached) - stays correct under rotate/scale since a rigid
	// transform about its own centroid leaves that centroid fixed. While dragging, the frame's
	// incremental delta is applied directly to windowState.structure (renderer hot path) for
	// immediate visual feedback; on release, the final result is committed to the domain structure
	// as one undoable command (RendererAtomEditCommands::TransformSelectedAtomsCommand).
	bool RendererPanel::renderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.selectedAtomIndices.empty() || windowState.camera == nullptr)
		{
			windowState.gizmoDragActive = false;
			return false;
		}

		// A modal/click-drag reads X/Y/Z (axis lock), digits/-/./Backspace/Enter (numeric override,
		// see CaptureGizmoNumericInput) and Escape (cancel) directly via ImGui::IsKeyPressed below -
		// none of that goes through an actual ImGui widget, so ImGui's own WantCaptureKeyboard (the
		// gate Application::dispatchEvent uses to stop app-wide shortcuts firing into a focused text
		// field) stays false and doesn't protect it. Without this, typing "1" to enter a distance
		// mid-drag also fired the bare "1" -> renderer.align_axis_a shortcut and yanked the camera.
		// One frame of lag (this only takes effect for the NEXT frame's input dispatch, since this
		// frame's events were already dispatched before Render() runs) is harmless for a multi-frame
		// drag - only the drag's very first keystroke could still leak through.
		if (windowState.fallbackGizmoDragging)
			ImGui::GetIO().WantCaptureKeyboard = true;

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
		// Enable(false) turns off ImGuizmo's own hit-test/drag path (see prior note: it was silently
		// live and fighting our fallback system for every click near the gizmo) - kept regardless of
		// whether Manipulate() below actually runs, in case anything else in this ID scope reads it.
		ImGuizmo::Enable(false);
		ImGuizmo::SetRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y);
		// TRANSLATE/SCALE only draw OUR OWN axis lines below (see "Blender-style axis indicators") -
		// ImGuizmo's native draw for these two ops also always includes its plane-drag quads
		// (DrawTranslationGizmo draws one per axis pair unconditionally, see TRANSLATE_PLANS in the
		// vendored source) even though this app has no plane-drag interaction at all, which is what
		// the recurring "why is this gray?" / "doesn't look right" reports were pointing at - a
		// translucent square implying a capability that doesn't exist. ROTATE has no such quads and
		// its native rings are still the only rest-state visual we have for that mode, so it keeps
		// using Manipulate() to draw.
		if (operation == ImGuizmo::ROTATE)
		{
			ImGuizmo::Manipulate(
				glm::value_ptr(view),
				glm::value_ptr(projection),
				operation,
				ImGuizmo::WORLD,
				glm::value_ptr(gizmoMatrix),
				glm::value_ptr(deltaMatrix));
		}
		ImGuizmo::PopID();

		// ImGuizmo::Manipulate() above is called purely to DRAW the handles - its own screen-space
		// picking (IsOver()/IsUsing()) is unreliable in this app in BOTH directions (confirmed via
		// synthetic clicks measured directly against screenshots earlier this session) and every
		// interaction below is driven by our own screen-space hit-test instead, ported from
		// Desktop/STUDIA/Degects-Studio (an earlier iteration of this project that hit the same
		// problem): false IsOver()==true blocked our own pick from ever starting so a click that
		// looked right on target did nothing at all; false IsOver()==false right as a fallback drag
		// began let handleAtomPick fire the same frame and silently re-pick whatever atom was
		// nearest the cursor - selection jumping mid-drag. World-space axes only (this gizmo never
		// runs in LOCAL mode).
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
		const bool pivotOnScreen = projectToScreen(pivot, pivotScreen);
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		// An atom actually under the cursor always wins over grabbing the gizmo (see
		// isAtomUnderScreenPosition) - only matters for STARTING a new hover/drag below, never checked
		// once fallbackGizmoDragging is already true so it can't interrupt a drag in progress.
		const bool atomUnderCursor = !windowState.fallbackGizmoDragging && isAtomUnderScreenPosition(windowState, imageOrigin, mousePos);
		const bool atomOrBondUnderCursor =
			atomUnderCursor || (!windowState.fallbackGizmoDragging && isBondUnderScreenPosition(windowState, imageOrigin, mousePos));
		constexpr float kPickMinDistance = 20.0f;
		// This band is now evaluated every frame (not just on click) to drive gizmoCapturing's
		// hover-suppression of atom-pick/box-select - the original 350px was sized for a one-off
		// click test and, applied continuously, ate clicks on any atom within ~350px of the pivot in
		// a dense structure ("selection acts erratic"). SetGizmoSizeClipSpace keeps the gizmo's own
		// drawn size constant in screen pixels regardless of zoom, so a smaller fixed band still
		// tracks the visible handles correctly at any zoom level.
		constexpr float kPickMaxDistance = 130.0f;

		constexpr glm::vec3 kWorldAxes[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
		constexpr ImU32 kAxisLockColors[3] = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		if (operation == ImGuizmo::ROTATE)
		{
			// No per-axis ring hit-test (would need ImGuizmo's internal ring radius, which isn't
			// exposed) - instead a trackball: grab anywhere in the pick band around the pivot and
			// drag freely, rotation axis = cross(camera-forward, screen-space drag direction), angle
			// proportional to drag distance. Visually looser than ImGuizmo's 3 discrete rings but
			// gives full 3D rotation control without needing their exact geometry.
			const float radial = pivotOnScreen ? glm::length(mousePos - pivotScreen) : -1.0f;
			const bool hoveringRing =
				pivotOnScreen && !atomOrBondUnderCursor && radial >= kPickMinDistance && radial <= kPickMaxDistance;

			if (!windowState.fallbackGizmoDragging && hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.fallbackGizmoDragging = true;
				windowState.fallbackGizmoAxis = -2; // sentinel: trackball rotate, not a translate/scale axis
				windowState.fallbackLastMousePos = mousePos;
				windowState.fallbackNumericInput.clear();
			}

			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis == -2)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
					const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

					const glm::vec2 delta = mousePos - windowState.fallbackLastMousePos;
					windowState.fallbackLastMousePos = mousePos;

					// Screen Y is flipped vs cameraUp (same convention as the pinned-measurement drag).
					const glm::vec3 dragWorldDir = cameraRight * delta.x - cameraUp * delta.y;
					const float dragLength = glm::length(dragWorldDir);
					if (dragLength > 0.0001f)
					{
						const glm::vec3 rotationAxis = glm::normalize(glm::cross(cameraForward, dragWorldDir));
						constexpr float kRadiansPerPixel = 0.006f;
						const glm::quat rotation = glm::angleAxis(glm::length(delta) * kRadiansPerPixel, rotationAxis);
						for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						{
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							RendererAtomData &atom = windowState.structure.atoms[atomIndex];
							atom.cartesianPosition = pivot + rotation * (atom.cartesianPosition - pivot);
						}
					}
					windowState.gizmoDragActive = true;
					return true;
				}

				windowState.fallbackGizmoDragging = false;
				windowState.fallbackGizmoAxis = -1;
			}

			// Blender-style modal axis-locked rotate: pressing X/Y/Z with no mouse button starts a
			// rotation constrained to that world axis, following the mouse's angular motion around
			// the pivot - same modal convention as translate/scale below (confirm with a left-click,
			// cancel with Escape/right-click). Angular instead of linear since this trackball has no
			// discrete per-axis handle to click, only the modal (keypress-first) path applies here.
			if (hovered && pivotOnScreen && !windowState.fallbackGizmoDragging)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					windowState.fallbackGizmoDragging = true;
					windowState.fallbackModalDrag = true;
					windowState.fallbackGizmoAxis = axis;
					windowState.fallbackLastMousePos = mousePos;
					windowState.fallbackNumericInput.clear();
					windowState.fallbackDragStartPositions.clear();
					for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						windowState.fallbackDragStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
					break;
				}
			}

			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis >= 0 && windowState.fallbackGizmoAxis <= 2)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					for (std::size_t i = 0;
						 i < windowState.selectedAtomIndices.size() && i < windowState.fallbackDragStartPositions.size();
						 ++i)
					{
						windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
							windowState.fallbackDragStartPositions[i];
					}
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackNumericInput.clear();
					windowState.gizmoDragActive = false;
					return true;
				}

				// Blender-style axis switch: pressing a different X/Y/Z re-points the lock without ending
				// the drag - matches translate/scale's override toggle, except rotate has no separate
				// "grabbed handle" baseline to release back to (this path only starts via modal X/Y/Z),
				// so re-pressing the SAME key is a no-op here instead of a release.
				constexpr ImGuiKey kRotateAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (axis != windowState.fallbackGizmoAxis && ImGui::IsKeyPressed(kRotateAxisKeys[axis], false))
						windowState.fallbackGizmoAxis = axis;
				}

				CaptureGizmoNumericInput(windowState);
				const bool numericActive = !windowState.fallbackNumericInput.empty();
				const bool numericConfirmed = numericActive &&
					(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
				const bool modalConfirmed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || numericConfirmed;
				if (pivotOnScreen)
				{
					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
					const glm::vec3 lockedAxisWorld = kWorldAxes[windowState.fallbackGizmoAxis];
					// Screen Y is flipped vs standard math convention, and a right-hand rotation
					// around an axis pointing away from the viewer (into the screen) reads as
					// clockwise on-screen - both flips cancel out when the axis points toward the
					// viewer instead, so only one sign check is needed here.
					const float rotationSign = glm::dot(lockedAxisWorld, cameraForward) >= 0.0f ? -1.0f : 1.0f;

					if (numericActive)
					{
						// Typed degrees apply ABSOLUTE from the pre-drag snapshot (not accumulated),
						// same reasoning as the translate/scale numeric path below. Pivot MUST be the
						// frozen start-of-drag centroid (MeanPosition of the snapshot), not the live
						// `pivot` above - see MeanPosition's comment for why mixing the two diverges.
						const glm::vec3 numericPivot = MeanPosition(windowState.fallbackDragStartPositions);
						const glm::quat rotation = glm::angleAxis(
							glm::radians(ParseTypedNumber(windowState.fallbackNumericInput)) * rotationSign, lockedAxisWorld);
						for (std::size_t i = 0;
							 i < windowState.selectedAtomIndices.size() && i < windowState.fallbackDragStartPositions.size();
							 ++i)
						{
							windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
								numericPivot + rotation * (windowState.fallbackDragStartPositions[i] - numericPivot);
						}
					}
					else
					{
						const glm::vec2 fromPivotLast = windowState.fallbackLastMousePos - pivotScreen;
						const glm::vec2 fromPivotNow = mousePos - pivotScreen;
						if (glm::length(fromPivotLast) > 1.0f && glm::length(fromPivotNow) > 1.0f)
						{
							const float lastAngle = std::atan2(fromPivotLast.y, fromPivotLast.x);
							const float nowAngle = std::atan2(fromPivotNow.y, fromPivotNow.x);
							float deltaAngle = nowAngle - lastAngle;
							while (deltaAngle > glm::pi<float>())
								deltaAngle -= glm::two_pi<float>();
							while (deltaAngle < -glm::pi<float>())
								deltaAngle += glm::two_pi<float>();

							const glm::quat rotation = glm::angleAxis(deltaAngle * rotationSign, lockedAxisWorld);
							for (const std::size_t atomIndex : windowState.selectedAtomIndices)
							{
								if (atomIndex >= windowState.structure.atoms.size())
									continue;
								RendererAtomData &atom = windowState.structure.atoms[atomIndex];
								atom.cartesianPosition = pivot + rotation * (atom.cartesianPosition - pivot);
							}
						}
					}
					windowState.fallbackLastMousePos = mousePos;

					ImDrawList *lockDrawList = ImGui::GetWindowDrawList();
					lockDrawList->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance,
						kAxisLockColors[windowState.fallbackGizmoAxis], 64, 3.0f);
					if (numericActive)
					{
						char label[64];
						std::snprintf(label, sizeof(label), "Rotate %c: %s deg", "XYZ"[windowState.fallbackGizmoAxis],
							windowState.fallbackNumericInput.c_str());
						ImGui::GetForegroundDrawList()->AddText(
							ImVec2(pivotScreen.x + 12.0f, pivotScreen.y - 24.0f), IM_COL32(255, 230, 60, 255), label);
					}
				}
				windowState.gizmoDragActive = true;

				if (modalConfirmed)
				{
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackNumericInput.clear();
					// Falls through to the shared commit block below instead of returning - matches
					// the translate/scale modal-confirm convention (no separate "release" frame).
				}
				else
				{
					return true;
				}
			}

			if (!windowState.gizmoDragActive)
				return hoveringRing;
		}
		else
		{
			// A 1-world-unit probe gives axis direction + a pixels-per-world ratio for this frame's
			// zoom. Computed every frame (not just at pick time) so the X/Y/Z axis-lock override
			// below can re-derive its direction as the camera moves during a drag, and so hovering
			// (no click yet) can still report an accurate axis for the capturing return value.
			glm::vec2 axisScreenDir[3];
			float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
			bool axisValid[3] = {false, false, false};
			if (pivotOnScreen)
			{
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
			}

			// Always-on red/green/blue axis indicators (item: "show red/green/blue axis") - this is
			// now the ONLY gizmo visual for translate/scale (see Manipulate() above), so it carries
			// the full "Blender-style" look on its own: a thick shaft plus a solid triangular
			// arrowhead per axis, no plane-drag quads (this app has no plane-drag interaction to
			// advertise). Short handles at rest, replaced by the full-length lock line below once a
			// drag actually locks onto one.
			if (pivotOnScreen && !windowState.fallbackGizmoDragging)
			{
				ImDrawList *axisDrawList = ImGui::GetWindowDrawList();
				constexpr float kArrowHeadLength = 16.0f;
				constexpr float kArrowHeadHalfWidth = 6.0f;
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis])
						continue;
					const glm::vec2 dir = axisScreenDir[axis];
					const glm::vec2 perp(-dir.y, dir.x);
					const glm::vec2 tip = glm::vec2(pivotScreen.x, pivotScreen.y) + dir * kPickMaxDistance;
					const glm::vec2 headBase = tip - dir * kArrowHeadLength;
					axisDrawList->AddLine(ImVec2(pivotScreen.x, pivotScreen.y), ImVec2(headBase.x, headBase.y),
						kAxisLockColors[axis], 3.5f);
					const glm::vec2 headLeft = headBase + perp * kArrowHeadHalfWidth;
					const glm::vec2 headRight = headBase - perp * kArrowHeadHalfWidth;
					axisDrawList->AddTriangleFilled(
						ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y),
						kAxisLockColors[axis]);
				}
				axisDrawList->AddCircleFilled(ImVec2(pivotScreen.x, pivotScreen.y), 5.0f, IM_COL32(235, 235, 235, 255));
			}

			// Blender-style modal move/scale: pressing X/Y/Z with NO mouse button held starts a drag
			// constrained to that axis immediately, following the mouse freely - confirmed with a
			// left-click, cancelled (reverting to the pre-drag snapshot) with Escape/right-click. This
			// is in addition to the click-and-drag-a-handle path below, not a replacement for it.
			if (hovered && pivotOnScreen && !windowState.fallbackGizmoDragging)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis] || !ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					windowState.fallbackGizmoDragging = true;
					windowState.fallbackModalDrag = true;
					windowState.fallbackGizmoAxis = axis;
					// Deliberately NOT set here - the "Blender-style axis lock" toggle loop below runs
					// this same frame (fallbackGizmoDragging is already true) and would immediately see
					// this same X/Y/Z keypress and toggle it straight back off (armed here, disarmed
					// there, both reading the same still-true IsKeyPressed for one physical press) if it
					// were pre-armed here too. Leaving it at its previous value (-1 the first time) lets
					// that loop be the ONLY place that arms it, so the lock/full-length line shows from
					// the very first press instead of needing a second one.
					windowState.fallbackLastMousePos = mousePos;
					windowState.fallbackNumericInput.clear();
					windowState.fallbackDragAxisScreenDir = axisScreenDir[axis];
					windowState.fallbackDragAxisWorldDir = kWorldAxes[axis];
					windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[axis]);
					windowState.fallbackDragStartPositions.clear();
					for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						windowState.fallbackDragStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
					break;
				}
			}

			int hoveredAxis = -1;
			if (pivotOnScreen && !windowState.fallbackGizmoDragging && !atomOrBondUnderCursor)
			{
				constexpr float kPickPerpTolerance = 16.0f;
				const glm::vec2 fromPivot = mousePos - pivotScreen;
				const float radial = glm::length(fromPivot);
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

				if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					windowState.fallbackGizmoDragging = true;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = hoveredAxis;
					windowState.fallbackAxisLockOverride = -1;
					windowState.fallbackLastMousePos = mousePos;
					windowState.fallbackNumericInput.clear();
					windowState.fallbackDragAxisScreenDir = axisScreenDir[hoveredAxis];
					windowState.fallbackDragAxisWorldDir = kWorldAxes[hoveredAxis];
					windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
					windowState.fallbackDragStartPositions.clear();
					for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						windowState.fallbackDragStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
				}
			}

			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis >= 0)
			{
				// Blender-style axis lock: X/Y/Z re-point the drag at a single world axis regardless
				// of which handle was originally grabbed; pressing the same key again releases the
				// override back to the grabbed axis. No local-space double-tap (Blender's XX/YY/ZZ) -
				// atoms carry no per-object orientation, so a "local" axis would just equal the
				// global one here.
				constexpr ImGuiKey kAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (ImGui::IsKeyPressed(kAxisKeys[axis], false))
						windowState.fallbackAxisLockOverride = windowState.fallbackAxisLockOverride == axis ? -1 : axis;
				}

				const int lockedAxis = windowState.fallbackAxisLockOverride;
				if (lockedAxis >= 0 && axisValid[lockedAxis])
				{
					windowState.fallbackDragAxisScreenDir = axisScreenDir[lockedAxis];
					windowState.fallbackDragAxisWorldDir = kWorldAxes[lockedAxis];
					windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[lockedAxis]);

					if (pivotOnScreen)
					{
						ImDrawList *drawList = ImGui::GetWindowDrawList();
						const glm::vec2 dir = axisScreenDir[lockedAxis];
						const ImVec2 farA(pivotScreen.x - dir.x * 10000.0f, pivotScreen.y - dir.y * 10000.0f);
						const ImVec2 farB(pivotScreen.x + dir.x * 10000.0f, pivotScreen.y + dir.y * 10000.0f);
						drawList->AddLine(farA, farB, kAxisLockColors[lockedAxis], 3.0f);
					}
				}

				// Cancel: Escape or right-click reverts to the pre-drag snapshot and ends the drag
				// without committing - works for both a modal drag and a click-drag (Blender lets you
				// abort either the same way), though in practice a click-drag's short lifetime makes
				// this mostly a modal-drag affordance.
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					for (std::size_t i = 0;
						 i < windowState.selectedAtomIndices.size() && i < windowState.fallbackDragStartPositions.size();
						 ++i)
					{
						windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
							windowState.fallbackDragStartPositions[i];
					}
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackAxisLockOverride = -1;
					windowState.fallbackNumericInput.clear();
					windowState.gizmoDragActive = false;
					return true;
				}

				CaptureGizmoNumericInput(windowState);
				const bool numericActive = !windowState.fallbackNumericInput.empty();
				const bool numericConfirmed = numericActive &&
					(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

				// A modal drag (started by X/Y/Z with no button held) applies every frame regardless
				// of mouse-button state and confirms on left-click; a click-drag keeps applying only
				// while the button stays down and commits on release - both fall through to the same
				// apply step below, they just disagree on when "still active" is true. Once a number is
				// being typed, the drag stays active regardless of mouse state (mirroring a modal drag)
				// until Enter confirms or Escape cancels - Blender lets you type a number after either
				// starting method.
				const bool modalConfirmed =
					(windowState.fallbackModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || numericConfirmed;
				const bool stillActive = numericActive ? true
					: (windowState.fallbackModalDrag ? !modalConfirmed : ImGui::IsMouseDown(ImGuiMouseButton_Left));

				if (stillActive || modalConfirmed)
				{
					float deltaOnAxisWorld;
					if (numericActive)
					{
						// Typed value applies ABSOLUTE from the pre-drag snapshot below (not accumulated
						// frame over frame the way the mouse-delta path is), so it's computed once here
						// and reused as an absolute offset/factor per atom.
						deltaOnAxisWorld = ParseTypedNumber(windowState.fallbackNumericInput);
					}
					else
					{
						const glm::vec2 delta = mousePos - windowState.fallbackLastMousePos;
						const float deltaOnAxisPixels = glm::dot(delta, windowState.fallbackDragAxisScreenDir);
						deltaOnAxisWorld = deltaOnAxisPixels / windowState.fallbackDragPixelsPerWorld;
					}
					windowState.fallbackLastMousePos = mousePos;

					if (operation == ImGuizmo::SCALE)
					{
						// Typed number IS the scale factor itself (Blender's "S 2 Enter" means 2x, not
						// 1+2) - everyone else still accumulates 1+delta incrementally onto the live
						// position, so factor and "from what" differ between the two paths.
						const float factor = numericActive ? glm::clamp(deltaOnAxisWorld, 0.05f, 20.0f)
															: glm::clamp(1.0f + deltaOnAxisWorld, 0.05f, 20.0f);
						// Numeric path needs the frozen start-of-drag centroid, not the live `pivot`
						// above - same feedback-loop reasoning as the rotate numeric path (see
						// MeanPosition's comment): mixing a frozen basePosition with a pivot recomputed
						// from that same frozen data's own (already-scaled) output diverges whenever the
						// typed factor is outside (0, 2).
						const glm::vec3 scalePivot = numericActive ? MeanPosition(windowState.fallbackDragStartPositions) : pivot;
						for (std::size_t i = 0; i < windowState.selectedAtomIndices.size(); ++i)
						{
							const std::size_t atomIndex = windowState.selectedAtomIndices[i];
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							const glm::vec3 &basePosition = numericActive && i < windowState.fallbackDragStartPositions.size()
								? windowState.fallbackDragStartPositions[i]
								: windowState.structure.atoms[atomIndex].cartesianPosition;
							const glm::vec3 relative = basePosition - scalePivot;
							const float along = glm::dot(relative, windowState.fallbackDragAxisWorldDir);
							const glm::vec3 perpendicular = relative - windowState.fallbackDragAxisWorldDir * along;
							windowState.structure.atoms[atomIndex].cartesianPosition =
								scalePivot + perpendicular + windowState.fallbackDragAxisWorldDir * (along * factor);
						}
					}
					else
					{
						const glm::vec3 worldDelta = windowState.fallbackDragAxisWorldDir * deltaOnAxisWorld;
						for (std::size_t i = 0; i < windowState.selectedAtomIndices.size(); ++i)
						{
							const std::size_t atomIndex = windowState.selectedAtomIndices[i];
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							windowState.structure.atoms[atomIndex].cartesianPosition = numericActive &&
									i < windowState.fallbackDragStartPositions.size()
								? windowState.fallbackDragStartPositions[i] + worldDelta
								: windowState.structure.atoms[atomIndex].cartesianPosition + worldDelta;
						}
					}
					windowState.gizmoDragActive = true;

					if (numericActive && pivotOnScreen)
					{
						char label[64];
						const int effectiveAxis = windowState.fallbackAxisLockOverride >= 0
							? windowState.fallbackAxisLockOverride
							: windowState.fallbackGizmoAxis;
						std::snprintf(label, sizeof(label), "%s %c: %s",
							operation == ImGuizmo::SCALE ? "Scale" : "Move",
							"XYZ"[effectiveAxis], windowState.fallbackNumericInput.c_str());
						ImGui::GetForegroundDrawList()->AddText(
							ImVec2(pivotScreen.x + 12.0f, pivotScreen.y - 24.0f), IM_COL32(255, 230, 60, 255), label);
					}

					if (modalConfirmed)
					{
						windowState.fallbackGizmoDragging = false;
						windowState.fallbackModalDrag = false;
						windowState.fallbackGizmoAxis = -1;
						windowState.fallbackAxisLockOverride = -1;
						windowState.fallbackNumericInput.clear();
						// Fall through to the shared commit block below instead of returning - a
						// confirming click ends the drag the same frame, no separate "release" frame
						// exists for a modal drag the way there is for a held button.
					}
					else
					{
						return true;
					}
				}
				else
				{
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackAxisLockOverride = -1;
				}
			}

			if (!windowState.gizmoDragActive)
				return hoveredAxis >= 0;
		}

		windowState.gizmoDragActive = false;

		Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
		if (commandRegistry == nullptr)
			return false;

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
		return false;
	}

	// Gizmo for the current label selection - any mix of pinned measurements (PinnedMeasurement::
	// worldOffset/rotationOffsetRadians/style.scale) and free labels (FreeLabel::worldPosition/
	// rotationRadians/style.scale) - sibling of renderTransformGizmo above, same screen-space
	// pick/drag philosophy as the atom gizmo (ImGuizmo's own picking is unreliable here too, see that
	// function's big comment). Pivot is the live centroid of every selected item, same "recomputed
	// every frame" convention the atom gizmo's pivot uses. Translate draws the familiar
	// shaft+arrowhead 3-axis handles and moves every selected item's own position field by the same
	// world-space delta (a rigid group move). Rotate/Scale use a single ring-drag around the pivot
	// instead - no per-axis handles, since a camera-facing billboard has only one meaningful rotation
	// axis (its own normal) and one meaningful scale (uniform glyph size) - and apply their delta to
	// each selected item's OWN rotation/scale field in place (spin/grow each independently, not
	// orbit their positions around the shared centroid the way a rigid-body atom rotate/scale would -
	// a label's rotation only ever means "this text's own orientation", never a position transform).
	// Pushes one undo snapshot at drag start via PushPinnedMeasurementUndoSnapshot (Ctrl+Alt+U/
	// Ctrl+Alt+Shift+U - see RendererEvents::Viewport::UndoLabelsRequested), covering both kinds
	// together (LabelUndoSnapshot) regardless of which are in this drag.
	bool RendererPanel::renderLabelTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.camera == nullptr)
		{
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoModalDrag = false;
			windowState.labelGizmoAxis = -1;
			return false;
		}

		if (windowState.selectedPinnedMeasurements.empty() && windowState.selectedFreeLabels.empty())
		{
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoModalDrag = false;
			windowState.labelGizmoAxis = -1;
			return false;
		}

		// A pin's live position is its label entity's TransformComponent (anchor + worldOffset, kept
		// current by SceneSystem::UpdateLabelTransforms, called once per frame before this) rather than
		// resolving the anchor itself - a free label has no such entity, so its position is just its
		// own worldPosition directly, always live, nothing to keep in sync.
		auto resolvePosition = [&](bool isPin, std::size_t index, glm::vec3 &outPosition) -> bool {
			if (isPin)
			{
				if (index >= windowState.pinnedMeasurements.size())
					return false;
				Entity labelEntity = windowState.sceneRegistry.LabelEntityAt(index);
				if (!labelEntity || !labelEntity.HasComponent<TransformComponent>())
					return false;
				outPosition = labelEntity.GetComponent<TransformComponent>().position;
				return true;
			}
			if (index >= windowState.freeLabels.size())
				return false;
			outPosition = windowState.freeLabels[index].worldPosition;
			return true;
		};
		// The mutable fields a drag actually writes to - PinnedMeasurement and FreeLabel are
		// different struct types so there's no single "the object" pointer to return, just the three
		// fields both happen to have.
		auto resolveMutableFields = [&](bool isPin, std::size_t index, glm::vec3 *&outPosition,
			float *&outRotation, float *&outScale) -> bool {
			if (isPin)
			{
				if (index >= windowState.pinnedMeasurements.size())
					return false;
				RendererWindowState::PinnedMeasurement &pin = windowState.pinnedMeasurements[index];
				outPosition = &pin.worldOffset;
				outRotation = &pin.rotationOffsetRadians;
				outScale = &pin.style.scale;
				return true;
			}
			if (index >= windowState.freeLabels.size())
				return false;
			RendererWindowState::FreeLabel &label = windowState.freeLabels[index];
			outPosition = &label.worldPosition;
			outRotation = &label.rotationRadians;
			outScale = &label.style.scale;
			return true;
		};

		glm::vec3 pivot(0.0f);
		int pivotCount = 0;
		for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
		{
			glm::vec3 position(0.0f);
			if (resolvePosition(true, pinIndex, position))
			{
				pivot += position;
				++pivotCount;
			}
		}
		for (const std::size_t labelIndex : windowState.selectedFreeLabels)
		{
			glm::vec3 position(0.0f);
			if (resolvePosition(false, labelIndex, position))
			{
				pivot += position;
				++pivotCount;
			}
		}
		if (pivotCount == 0)
			return false;
		pivot /= static_cast<float>(pivotCount);

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
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
		const bool pivotOnScreen = projectToScreen(pivot, pivotScreen);
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

		// Smaller pick band than the atom gizmo's kPickMaxDistance (130px) - a label has no atom
		// underneath competing for the same screen space, so there is no need for as much clearance.
		// (Was 14/70 - bumped up, the gizmo was cramped enough to overlap the label's own text.)
		constexpr float kPickMinDistance = 20.0f;
		constexpr float kPickMaxDistance = 100.0f;

		// Snapshots every selected item's current position/rotation/scale into
		// windowState.labelGizmoDragTargets (for cancel-revert and Scale's ratio-from-start math) and
		// pushes the one undo entry for the whole drag - called once, right as a ring or axis drag
		// starts.
		auto beginDrag = [&]() {
			windowState.labelGizmoDragTargets.clear();
			auto captureTarget = [&](bool isPin, std::size_t index) {
				glm::vec3 *positionPtr = nullptr;
				float *rotationPtr = nullptr;
				float *scalePtr = nullptr;
				if (!resolveMutableFields(isPin, index, positionPtr, rotationPtr, scalePtr))
					return;
				windowState.labelGizmoDragTargets.push_back(
					RendererWindowState::LabelGizmoDragTarget{isPin, index, *positionPtr, *rotationPtr, *scalePtr});
			};
			for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
				captureTarget(true, pinIndex);
			for (const std::size_t labelIndex : windowState.selectedFreeLabels)
				captureTarget(false, labelIndex);
			PushPinnedMeasurementUndoSnapshot(windowState);
		};

		if (windowState.gizmoOperation == GizmoOperation::Rotate || windowState.gizmoOperation == GizmoOperation::Scale)
		{
			const bool isRotate = windowState.gizmoOperation == GizmoOperation::Rotate;
			constexpr ImU32 kRingColor = IM_COL32(235, 235, 235, 200);
			constexpr ImU32 kRingActiveColor = IM_COL32(255, 200, 60, 220);

			if (pivotOnScreen && !windowState.labelGizmoDragging)
				ImGui::GetWindowDrawList()->AddCircle(
					ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingColor, 48, 2.0f);

			const float radialNow = pivotOnScreen ? glm::length(mousePos - pivotScreen) : -1.0f;
			const bool hoveringRing = hovered && pivotOnScreen && !windowState.labelGizmoDragging &&
				radialNow >= kPickMinDistance && radialNow <= kPickMaxDistance;

			if (hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				beginDrag();
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoModalDrag = false; // no keyboard-modal path for Rotate/Scale
				windowState.labelGizmoAxis = -2; // sentinel: ring drag, no X/Y/Z handle
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragStartRadial = std::max(kPickMinDistance, radialNow);
			}

			if (windowState.labelGizmoDragging && windowState.labelGizmoAxis == -2)
			{
				if (pivotOnScreen)
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingActiveColor, 48, 3.0f);

				// Cancel: Escape or right-click reverts every target to its pre-drag snapshot, same
				// convention as the atom gizmo's fallback drag.
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					for (const RendererWindowState::LabelGizmoDragTarget &target : windowState.labelGizmoDragTargets)
					{
						glm::vec3 *positionPtr = nullptr;
						float *rotationPtr = nullptr;
						float *scalePtr = nullptr;
						if (!resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
							continue;
						*rotationPtr = target.startRotation;
						*scalePtr = target.startScale;
					}
					windowState.labelGizmoDragging = false;
					windowState.labelGizmoAxis = -1;
					return true;
				}

				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					if (isRotate)
					{
						// atan2's y negated: screen space is y-down, while the billboard's own local
						// "up" (matched to cameraUp in labels.vert) is y-up - without this the drag
						// would feel mirrored (drag clockwise on screen, label spins the other way).
						const glm::vec2 fromPivotLast = windowState.labelGizmoLastMousePos - pivotScreen;
						const glm::vec2 fromPivotNow = mousePos - pivotScreen;
						if (glm::length(fromPivotLast) > 1.0f && glm::length(fromPivotNow) > 1.0f)
						{
							const float lastAngle = std::atan2(-fromPivotLast.y, fromPivotLast.x);
							const float nowAngle = std::atan2(-fromPivotNow.y, fromPivotNow.x);
							float deltaAngle = nowAngle - lastAngle;
							while (deltaAngle > glm::pi<float>())
								deltaAngle -= glm::two_pi<float>();
							while (deltaAngle < -glm::pi<float>())
								deltaAngle += glm::two_pi<float>();
							// Incremental (each selected item spins by the same per-frame delta), unlike
							// Scale below which recomputes from the frozen start value every frame.
							for (const RendererWindowState::LabelGizmoDragTarget &target :
								windowState.labelGizmoDragTargets)
							{
								glm::vec3 *positionPtr = nullptr;
								float *rotationPtr = nullptr;
								float *scalePtr = nullptr;
								if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
									*rotationPtr += deltaAngle;
							}
						}
					}
					else
					{
						// Blender S-style: scale ratio is the current radial distance from the pivot
						// over the distance at drag start, not a per-frame delta - dragging back to the
						// start radius always returns exactly to each target's own start scale.
						const float currentRadial = std::max(1.0f, glm::length(mousePos - pivotScreen));
						const float ratio = currentRadial / windowState.labelGizmoDragStartRadial;
						for (const RendererWindowState::LabelGizmoDragTarget &target :
							windowState.labelGizmoDragTargets)
						{
							glm::vec3 *positionPtr = nullptr;
							float *rotationPtr = nullptr;
							float *scalePtr = nullptr;
							if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
								*scalePtr = glm::clamp(target.startScale * ratio, 0.1f, 8.0f);
						}
					}
					windowState.labelGizmoLastMousePos = mousePos;
					return true;
				}

				windowState.labelGizmoDragging = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			return hoveringRing;
		}

		// Translate (default / GizmoOperation::Translate) - 3-axis arrow handles, same shape as the
		// atom gizmo's own translate branch.
		constexpr glm::vec3 kWorldAxes[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
		constexpr ImU32 kAxisLockColors[3] = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		glm::vec2 axisScreenDir[3];
		float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
		bool axisValid[3] = {false, false, false};
		if (pivotOnScreen)
		{
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
		}

		if (pivotOnScreen && !windowState.labelGizmoDragging)
		{
			ImDrawList *axisDrawList = ImGui::GetWindowDrawList();
			constexpr float kArrowHeadLength = 16.0f;
			constexpr float kArrowHeadHalfWidth = 7.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axisValid[axis])
					continue;
				const glm::vec2 dir = axisScreenDir[axis];
				const glm::vec2 perp(-dir.y, dir.x);
				const glm::vec2 tip = glm::vec2(pivotScreen.x, pivotScreen.y) + dir * kPickMaxDistance;
				const glm::vec2 headBase = tip - dir * kArrowHeadLength;
				axisDrawList->AddLine(
					ImVec2(pivotScreen.x, pivotScreen.y), ImVec2(headBase.x, headBase.y), kAxisLockColors[axis], 2.5f);
				const glm::vec2 headLeft = headBase + perp * kArrowHeadHalfWidth;
				const glm::vec2 headRight = headBase - perp * kArrowHeadHalfWidth;
				axisDrawList->AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y),
					kAxisLockColors[axis]);
			}
		}

		int hoveredAxis = -1;
		if (hovered && pivotOnScreen && !windowState.labelGizmoDragging)
		{
			constexpr float kPickPerpTolerance = 16.0f;
			const glm::vec2 fromPivot = mousePos - pivotScreen;
			const float radial = glm::length(fromPivot);
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

			if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				beginDrag();
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoModalDrag = false;
				windowState.labelGizmoAxis = hoveredAxis;
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragAxisScreenDir = axisScreenDir[hoveredAxis];
				windowState.labelGizmoDragAxisWorldDir = kWorldAxes[hoveredAxis];
				windowState.labelGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
			}
		}

		// Blender-style modal axis-locked translate: pressing X/Y/Z with no mouse button held starts a
		// drag constrained to that world axis, following the mouse without needing to click the handle
		// first - same modal convention as the atom gizmo's fallbackModalDrag (confirm with a
		// left-click, cancel with Escape/right-click, handled below).
		if (hovered && pivotOnScreen && !windowState.labelGizmoDragging)
		{
			constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axisValid[axis] || !ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
					continue;
				beginDrag();
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoModalDrag = true;
				windowState.labelGizmoAxis = axis;
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragAxisScreenDir = axisScreenDir[axis];
				windowState.labelGizmoDragAxisWorldDir = kWorldAxes[axis];
				windowState.labelGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[axis]);
				break;
			}
		}

		if (windowState.labelGizmoDragging && windowState.labelGizmoAxis >= 0)
		{
			if (pivotOnScreen)
			{
				ImDrawList *drawList = ImGui::GetWindowDrawList();
				const glm::vec2 dir = windowState.labelGizmoDragAxisScreenDir;
				const ImVec2 farA(pivotScreen.x - dir.x * 10000.0f, pivotScreen.y - dir.y * 10000.0f);
				const ImVec2 farB(pivotScreen.x + dir.x * 10000.0f, pivotScreen.y + dir.y * 10000.0f);
				drawList->AddLine(farA, farB, kAxisLockColors[windowState.labelGizmoAxis], 2.5f);
			}

			// Cancel: Escape or right-click reverts every target to its pre-drag snapshot, same
			// convention as the atom gizmo's fallback drag.
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				for (const RendererWindowState::LabelGizmoDragTarget &target : windowState.labelGizmoDragTargets)
				{
					glm::vec3 *positionPtr = nullptr;
					float *rotationPtr = nullptr;
					float *scalePtr = nullptr;
					if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
						*positionPtr = target.startPosition;
				}
				windowState.labelGizmoDragging = false;
				windowState.labelGizmoModalDrag = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			// Modal drag (started via X/Y/Z, see above) confirms on a left-click instead of on
			// release - the live value below is already applied, so confirming is just stopping.
			if (windowState.labelGizmoModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.labelGizmoDragging = false;
				windowState.labelGizmoModalDrag = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			if (windowState.labelGizmoModalDrag || ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const glm::vec2 delta = mousePos - windowState.labelGizmoLastMousePos;
				windowState.labelGizmoLastMousePos = mousePos;
				const float deltaOnAxisPixels = glm::dot(delta, windowState.labelGizmoDragAxisScreenDir);
				const float deltaOnAxisWorld = deltaOnAxisPixels / windowState.labelGizmoDragPixelsPerWorld;
				const glm::vec3 worldDelta = windowState.labelGizmoDragAxisWorldDir * deltaOnAxisWorld;
				// Rigid group move - every selected item's own position field shifts by the same
				// world-space delta, incrementally each frame (matches the single-select code this
				// replaced; see the Rotate/Scale branch above for why rotation/scale instead apply
				// per-item in place rather than moving anyone's position).
				for (const RendererWindowState::LabelGizmoDragTarget &target : windowState.labelGizmoDragTargets)
				{
					glm::vec3 *positionPtr = nullptr;
					float *rotationPtr = nullptr;
					float *scalePtr = nullptr;
					if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
						*positionPtr += worldDelta;
				}
				return true;
			}

			// Button released (non-modal only - a modal drag never reaches here, it only stops via
			// confirm/cancel above) - commit (the drag already mutated positions live, nothing further
			// to apply) and consume this one release frame same as the atom gizmo does.
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoAxis = -1;
			return true;
		}

		return hoveredAxis >= 0;
	}

	// Drawn translate/rotate/scale gizmo for the current SceneArrow selection - sibling of
	// renderLabelTransformGizmo above (own state, no ICommand/UndoStack, PushPinnedMeasurementUndoSnapshot
	// on drag start). Translate differs from every other gizmo in this file: exactly one arrow selected
	// draws THREE pick points (Start, End, and the midpoint for a rigid whole-arrow move) instead of one,
	// since an arrow (unlike an atom or a label) is defined by two independent positions. More than one
	// arrow selected collapses to a single group-centroid pivot (every selected arrow's both endpoints
	// move together), matching the existing raw-drag system's own "multi-selection is always rigid" rule
	// (see SceneArrowDragTarget above). Modal X/Y/Z axis-lock-start (no mouse button, follows the mouse
	// immediately) only applies to the midpoint/group pivot - Start/End are click-and-drag only, a
	// deliberate scope trim (the common "nudge one endpoint" case is already well served by a direct
	// click-drag; modal start earns its keep on the whole-arrow move, the more frequent action). Rotate is
	// a free trackball (grab anywhere in the ring band, drag freely - same cross(camera-forward,
	// drag-direction) math as the atom gizmo's own trackball path, no axis-lock modal) around either the
	// selection's own centroid or windowState.cursor3DPosition, per sceneArrowGizmoPivotMode (the toolbar
	// toggle). Scale has no pivot concept at all - it only multiplies shaftWidth/headWidth/headLength in
	// place (per the user's explicit "thickness only, never touches start/end" decision).
	bool RendererPanel::renderSceneArrowTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.selectedSceneArrows.empty() || windowState.camera == nullptr)
		{
			windowState.sceneArrowGizmoDragging = false;
			windowState.sceneArrowGizmoModalDrag = false;
			windowState.sceneArrowGizmoAxis = -1;
			return false;
		}

		glm::vec3 centroid(0.0f);
		int centroidCount = 0;
		for (const std::size_t index : windowState.selectedSceneArrows)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			centroid += windowState.sceneArrows[index].start;
			centroid += windowState.sceneArrows[index].end;
			centroidCount += 2;
		}
		if (centroidCount == 0)
			return false;
		centroid /= static_cast<float>(centroidCount);

		const bool isSingleArrow = windowState.selectedSceneArrows.size() == 1 &&
			windowState.selectedSceneArrows.front() < windowState.sceneArrows.size();

		using DragTarget = RendererWindowState::SceneArrowDragTarget;
		constexpr glm::vec3 kWorldAxes[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
		constexpr ImU32 kAxisLockColors[3] = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		// Which single Start/End/midpoint point currently owns the drawn axis-triad (item 3 of this
		// round's feedback: showing all 3 at once on a single arrow was cluttered/ambiguous) - resets
		// to the whole-arrow midpoint whenever the selection changes to a different arrow (or stops
		// being a single-arrow selection), so switching arrows never leaves a stale sub-target active.
		const std::size_t currentSingleArrowIndex =
			isSingleArrow ? windowState.selectedSceneArrows.front() : static_cast<std::size_t>(-1);
		if (windowState.sceneArrowGizmoActiveArrowIndex != currentSingleArrowIndex)
		{
			windowState.sceneArrowGizmoActiveArrowIndex = currentSingleArrowIndex;
			windowState.sceneArrowGizmoActiveTarget = DragTarget::Both;
		}

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * view;
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
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

		// Snapshots every selected arrow's start/end/shaftWidth/headWidth/headLength (for cancel-revert
		// and Scale's ratio-from-start math) and pushes the one undo entry for the whole drag - called
		// once, right as a handle/ring drag starts.
		auto beginDrag = [&]() {
			windowState.sceneArrowGizmoDragTargets.clear();
			for (const std::size_t index : windowState.selectedSceneArrows)
			{
				if (index >= windowState.sceneArrows.size())
					continue;
				const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[index];
				RendererWindowState::SceneArrowGizmoDragTarget target;
				target.index = index;
				target.startPosition = arrow.start;
				target.endPosition = arrow.end;
				target.startShaftWidth = arrow.style.shaftWidth;
				target.startHeadWidth = arrow.style.headWidth;
				target.startHeadLength = arrow.style.headLength;
				windowState.sceneArrowGizmoDragTargets.push_back(target);
			}
			PushPinnedMeasurementUndoSnapshot(windowState);
		};

		// Shared by both rotate-cancel paths below (free trackball and axis-locked) - restores every
		// dragged arrow's pre-drag start/end/thickness from the beginDrag() snapshot.
		auto revertRotateOrScale = [&]() {
			for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
			{
				if (target.index >= windowState.sceneArrows.size())
					continue;
				RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[target.index];
				arrow.start = target.startPosition;
				arrow.end = target.endPosition;
				arrow.style.shaftWidth = target.startShaftWidth;
				arrow.style.headWidth = target.startHeadWidth;
				arrow.style.headLength = target.startHeadLength;
			}
		};

		if (windowState.gizmoOperation == GizmoOperation::Rotate || windowState.gizmoOperation == GizmoOperation::Scale)
		{
			const bool isRotate = windowState.gizmoOperation == GizmoOperation::Rotate;
			const glm::vec3 pivot = (isRotate &&
										 windowState.sceneArrowGizmoPivotMode == RendererWindowState::ArrowGizmoPivotMode::Cursor3D &&
										 windowState.cursor3DPlaced)
				? windowState.cursor3DPosition
				: centroid;

			glm::vec2 pivotScreen(0.0f);
			const bool pivotOnScreen = projectToScreen(pivot, pivotScreen);

			constexpr float kPickMinDistance = 20.0f;
			constexpr float kPickMaxDistance = 100.0f;
			constexpr ImU32 kRingColor = IM_COL32(235, 235, 235, 200);
			constexpr ImU32 kRingActiveColor = IM_COL32(255, 200, 60, 220);

			if (pivotOnScreen && !windowState.sceneArrowGizmoDragging)
				ImGui::GetWindowDrawList()->AddCircle(
					ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingColor, 48, 2.0f);

			const float radialNow = pivotOnScreen ? glm::length(mousePos - pivotScreen) : -1.0f;
			const bool hoveringRing = hovered && pivotOnScreen && !windowState.sceneArrowGizmoDragging &&
				radialNow >= kPickMinDistance && radialNow <= kPickMaxDistance;

			if (hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				beginDrag();
				windowState.sceneArrowGizmoDragging = true;
				windowState.sceneArrowGizmoModalDrag = false;
				windowState.sceneArrowGizmoAxis = -2; // sentinel: ring drag, no X/Y/Z handle
				windowState.sceneArrowGizmoLastMousePos = mousePos;
				windowState.sceneArrowGizmoDragStartRadial = std::max(kPickMinDistance, radialNow);
			}

			// Blender-style modal axis-locked rotate (item 4 of this round's feedback: the free trackball
			// had no way to constrain which axis the arrow spun around) - pressing X/Y/Z with no mouse
			// button down starts a rotation locked to that world axis, following the mouse's angular
			// motion around the pivot; same modal convention as the atom gizmo's rotate (see
			// renderTransformGizmo's ROTATE branch, ported here) - confirm with left-click, cancel with
			// Escape/right-click, re-press a different axis key mid-drag to switch it.
			if (isRotate && hovered && pivotOnScreen && !windowState.sceneArrowGizmoDragging)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					beginDrag();
					windowState.sceneArrowGizmoDragging = true;
					windowState.sceneArrowGizmoModalDrag = true;
					windowState.sceneArrowGizmoAxis = axis;
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					break;
				}
			}

			if (windowState.sceneArrowGizmoDragging && windowState.sceneArrowGizmoAxis >= 0 && windowState.sceneArrowGizmoAxis <= 2)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					revertRotateOrScale();
					windowState.sceneArrowGizmoDragging = false;
					windowState.sceneArrowGizmoModalDrag = false;
					windowState.sceneArrowGizmoAxis = -1;
					return true;
				}

				constexpr ImGuiKey kRotateAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (axis != windowState.sceneArrowGizmoAxis && ImGui::IsKeyPressed(kRotateAxisKeys[axis], false))
						windowState.sceneArrowGizmoAxis = axis;
				}

				const bool confirmed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
				if (pivotOnScreen)
				{
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kAxisLockColors[windowState.sceneArrowGizmoAxis],
						48, 3.0f);

					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
					const glm::vec3 lockedAxisWorld = kWorldAxes[windowState.sceneArrowGizmoAxis];
					// Screen Y is flipped vs standard math convention, and a right-hand rotation around
					// an axis pointing away from the viewer reads as clockwise on-screen - both flips
					// cancel out when the axis points toward the viewer instead (same derivation as the
					// atom gizmo's locked rotate).
					const float rotationSign = glm::dot(lockedAxisWorld, cameraForward) >= 0.0f ? -1.0f : 1.0f;

					const glm::vec2 fromPivotLast = windowState.sceneArrowGizmoLastMousePos - pivotScreen;
					const glm::vec2 fromPivotNow = mousePos - pivotScreen;
					if (glm::length(fromPivotLast) > 1.0f && glm::length(fromPivotNow) > 1.0f)
					{
						const float lastAngle = std::atan2(fromPivotLast.y, fromPivotLast.x);
						const float nowAngle = std::atan2(fromPivotNow.y, fromPivotNow.x);
						float deltaAngle = nowAngle - lastAngle;
						while (deltaAngle > glm::pi<float>())
							deltaAngle -= glm::two_pi<float>();
						while (deltaAngle < -glm::pi<float>())
							deltaAngle += glm::two_pi<float>();

						const glm::quat rotation = glm::angleAxis(deltaAngle * rotationSign, lockedAxisWorld);
						for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
						{
							if (target.index >= windowState.sceneArrows.size())
								continue;
							RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[target.index];
							arrow.start = pivot + rotation * (arrow.start - pivot);
							arrow.end = pivot + rotation * (arrow.end - pivot);
						}
					}
					windowState.sceneArrowGizmoLastMousePos = mousePos;
				}

				if (confirmed)
				{
					windowState.sceneArrowGizmoDragging = false;
					windowState.sceneArrowGizmoModalDrag = false;
					windowState.sceneArrowGizmoAxis = -1;
					return true;
				}
				return true;
			}

			if (windowState.sceneArrowGizmoDragging && windowState.sceneArrowGizmoAxis == -2)
			{
				if (pivotOnScreen)
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingActiveColor, 48, 3.0f);

				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					revertRotateOrScale();
					windowState.sceneArrowGizmoDragging = false;
					windowState.sceneArrowGizmoAxis = -1;
					return true;
				}

				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					if (isRotate)
					{
						const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
						const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
						const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

						const glm::vec2 delta = mousePos - windowState.sceneArrowGizmoLastMousePos;
						windowState.sceneArrowGizmoLastMousePos = mousePos;
						const glm::vec3 dragWorldDir = cameraRight * delta.x - cameraUp * delta.y;
						const float dragLength = glm::length(dragWorldDir);
						if (dragLength > 0.0001f)
						{
							const glm::vec3 rotationAxis = glm::normalize(glm::cross(cameraForward, dragWorldDir));
							constexpr float kRadiansPerPixel = 0.006f;
							const glm::quat rotation = glm::angleAxis(glm::length(delta) * kRadiansPerPixel, rotationAxis);
							for (const RendererWindowState::SceneArrowGizmoDragTarget &target :
								windowState.sceneArrowGizmoDragTargets)
							{
								if (target.index >= windowState.sceneArrows.size())
									continue;
								RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[target.index];
								arrow.start = pivot + rotation * (arrow.start - pivot);
								arrow.end = pivot + rotation * (arrow.end - pivot);
							}
						}
					}
					else
					{
						// Blender S-style: scale ratio is the current radial distance from the pivot over
						// the distance at drag start, not a per-frame delta - dragging back to the start
						// radius always returns exactly to each target's own start thickness.
						const float currentRadial = std::max(1.0f, glm::length(mousePos - pivotScreen));
						const float ratio = currentRadial / windowState.sceneArrowGizmoDragStartRadial;
						for (const RendererWindowState::SceneArrowGizmoDragTarget &target :
							windowState.sceneArrowGizmoDragTargets)
						{
							if (target.index >= windowState.sceneArrows.size())
								continue;
							RendererWindowState::ArrowStyle &style = windowState.sceneArrows[target.index].style;
							style.shaftWidth = std::clamp(target.startShaftWidth * ratio, 0.005f, 1.0f);
							style.headWidth = std::clamp(target.startHeadWidth * ratio, 0.01f, 1.0f);
							style.headLength = std::clamp(target.startHeadLength * ratio, 0.01f, 2.0f);
						}
					}
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					return true;
				}

				windowState.sceneArrowGizmoDragging = false;
				windowState.sceneArrowGizmoAxis = -1;
				return true;
			}

			return hoveringRing;
		}

		// Translate. kWorldAxes/kAxisLockColors/DragTarget are declared earlier in this function (the
		// Rotate branch's axis-lock needs them too).

		// Applies the axis-drag's per-frame world delta to whichever field(s) sceneArrowGizmoEndpointTarget
		// says are active - shared by both the click-drag and modal-drag continuation below.
		auto applyTranslateDelta = [&](const glm::vec3 &worldDelta) {
			if (windowState.sceneArrowGizmoEndpointTarget == DragTarget::Both)
			{
				for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
				{
					if (target.index >= windowState.sceneArrows.size())
						continue;
					windowState.sceneArrows[target.index].start += worldDelta;
					windowState.sceneArrows[target.index].end += worldDelta;
				}
				return;
			}
			if (windowState.sceneArrowGizmoDragTargets.empty())
				return;
			const std::size_t index = windowState.sceneArrowGizmoDragTargets.front().index;
			if (index >= windowState.sceneArrows.size())
				return;
			if (windowState.sceneArrowGizmoEndpointTarget == DragTarget::Start)
				windowState.sceneArrows[index].start += worldDelta;
			else
				windowState.sceneArrows[index].end += worldDelta;
		};
		auto revertTranslate = [&]() {
			for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
			{
				if (target.index >= windowState.sceneArrows.size())
					continue;
				windowState.sceneArrows[target.index].start = target.startPosition;
				windowState.sceneArrows[target.index].end = target.endPosition;
			}
		};

		if (windowState.sceneArrowGizmoDragging)
		{
			glm::vec3 activePivotWorld = centroid;
			if (windowState.sceneArrowGizmoEndpointTarget != DragTarget::Both && !windowState.sceneArrowGizmoDragTargets.empty())
			{
				const std::size_t index = windowState.sceneArrowGizmoDragTargets.front().index;
				if (index < windowState.sceneArrows.size())
					activePivotWorld = windowState.sceneArrowGizmoEndpointTarget == DragTarget::Start
						? windowState.sceneArrows[index].start
						: windowState.sceneArrows[index].end;
			}
			glm::vec2 activePivotScreen(0.0f);
			if (projectToScreen(activePivotWorld, activePivotScreen))
			{
				ImDrawList *drawList = ImGui::GetWindowDrawList();
				const glm::vec2 dir = windowState.sceneArrowGizmoDragAxisScreenDir;
				const ImVec2 farA(activePivotScreen.x - dir.x * 10000.0f, activePivotScreen.y - dir.y * 10000.0f);
				const ImVec2 farB(activePivotScreen.x + dir.x * 10000.0f, activePivotScreen.y + dir.y * 10000.0f);
				drawList->AddLine(farA, farB, kAxisLockColors[windowState.sceneArrowGizmoAxis], 2.5f);
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				revertTranslate();
				windowState.sceneArrowGizmoDragging = false;
				windowState.sceneArrowGizmoModalDrag = false;
				windowState.sceneArrowGizmoAxis = -1;
				return true;
			}

			if (windowState.sceneArrowGizmoModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.sceneArrowGizmoDragging = false;
				windowState.sceneArrowGizmoModalDrag = false;
				windowState.sceneArrowGizmoAxis = -1;
				return true;
			}

			if (windowState.sceneArrowGizmoModalDrag || ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const glm::vec2 delta = mousePos - windowState.sceneArrowGizmoLastMousePos;
				windowState.sceneArrowGizmoLastMousePos = mousePos;
				const float deltaOnAxisPixels = glm::dot(delta, windowState.sceneArrowGizmoDragAxisScreenDir);
				const float deltaOnAxisWorld = deltaOnAxisPixels / windowState.sceneArrowGizmoDragPixelsPerWorld;
				applyTranslateDelta(windowState.sceneArrowGizmoDragAxisWorldDir * deltaOnAxisWorld);
				return true;
			}

			windowState.sceneArrowGizmoDragging = false;
			windowState.sceneArrowGizmoAxis = -1;
			return true;
		}

		// Not dragging - draw + hit-test each candidate pivot (Start/End/midpoint for a single selected
		// arrow, or just the group centroid for a multi-selection).
		struct Candidate
		{
			glm::vec3 world;
			DragTarget target;
			float pickMaxDistance;
			bool allowModal;
		};
		std::vector<Candidate> candidates;
		if (isSingleArrow)
		{
			const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[windowState.selectedSceneArrows.front()];
			candidates.push_back({arrow.start, DragTarget::Start, 60.0f, false});
			candidates.push_back({arrow.end, DragTarget::End, 60.0f, false});
			candidates.push_back({(arrow.start + arrow.end) * 0.5f, DragTarget::Both, 100.0f, true});
		}
		else
		{
			candidates.push_back({centroid, DragTarget::Both, 100.0f, true});
		}

		constexpr float kPickMinDistance = 20.0f;
		bool anyCandidateHovered = false;
		for (const Candidate &candidate : candidates)
		{
			glm::vec2 pivotScreen(0.0f);
			if (!projectToScreen(candidate.world, pivotScreen))
				continue;

			// Item 3 of this round's feedback: showing Start/End/midpoint's full axis-triad all at once
			// on a single selected arrow was cluttered and ambiguous about which drag would move what.
			// Only the active target (sceneArrowGizmoActiveTarget, reset to Both/midpoint on selection
			// change above) gets drawn/hit-tested as a real gizmo below - the other candidates on a
			// single-arrow selection are just plain click-to-activate dots.
			if (isSingleArrow && candidate.target != windowState.sceneArrowGizmoActiveTarget)
			{
				ImGui::GetWindowDrawList()->AddCircleFilled(
					ImVec2(pivotScreen.x, pivotScreen.y), 5.0f, IM_COL32(190, 190, 190, 190));
				if (hovered)
				{
					constexpr float kInactivePickRadius = 10.0f;
					if (glm::length(mousePos - pivotScreen) <= kInactivePickRadius)
					{
						anyCandidateHovered = true;
						if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
							windowState.sceneArrowGizmoActiveTarget = candidate.target;
					}
				}
				continue;
			}

			glm::vec2 axisScreenDir[3];
			float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
			bool axisValid[3] = {false, false, false};
			for (int axis = 0; axis < 3; ++axis)
			{
				glm::vec2 probeScreen;
				if (!projectToScreen(candidate.world + kWorldAxes[axis], probeScreen))
					continue;
				const glm::vec2 axisVec = probeScreen - pivotScreen;
				const float axisPixels = glm::length(axisVec);
				if (axisPixels < 1.0f)
					continue;
				axisScreenDir[axis] = axisVec / axisPixels;
				axisPixelsPerWorld[axis] = axisPixels;
				axisValid[axis] = true;
			}

			ImDrawList *axisDrawList = ImGui::GetWindowDrawList();
			constexpr float kArrowHeadLength = 14.0f;
			constexpr float kArrowHeadHalfWidth = 6.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axisValid[axis])
					continue;
				const glm::vec2 dir = axisScreenDir[axis];
				const glm::vec2 perp(-dir.y, dir.x);
				const glm::vec2 tip = glm::vec2(pivotScreen.x, pivotScreen.y) + dir * candidate.pickMaxDistance;
				const glm::vec2 headBase = tip - dir * kArrowHeadLength;
				axisDrawList->AddLine(
					ImVec2(pivotScreen.x, pivotScreen.y), ImVec2(headBase.x, headBase.y), kAxisLockColors[axis], 2.5f);
				const glm::vec2 headLeft = headBase + perp * kArrowHeadHalfWidth;
				const glm::vec2 headRight = headBase - perp * kArrowHeadHalfWidth;
				axisDrawList->AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y), kAxisLockColors[axis]);
			}
			axisDrawList->AddCircleFilled(ImVec2(pivotScreen.x, pivotScreen.y), 4.0f, IM_COL32(235, 235, 235, 255));

			int hoveredAxis = -1;
			if (hovered)
			{
				constexpr float kPickPerpTolerance = 14.0f;
				const glm::vec2 fromPivot = mousePos - pivotScreen;
				const float radial = glm::length(fromPivot);
				float bestPerp = kPickPerpTolerance;
				if (radial >= kPickMinDistance && radial <= candidate.pickMaxDistance)
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
			}

			if (hoveredAxis >= 0)
			{
				anyCandidateHovered = true;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					beginDrag();
					windowState.sceneArrowGizmoDragging = true;
					windowState.sceneArrowGizmoModalDrag = false;
					windowState.sceneArrowGizmoAxis = hoveredAxis;
					windowState.sceneArrowGizmoEndpointTarget = candidate.target;
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					windowState.sceneArrowGizmoDragAxisScreenDir = axisScreenDir[hoveredAxis];
					windowState.sceneArrowGizmoDragAxisWorldDir = kWorldAxes[hoveredAxis];
					windowState.sceneArrowGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
					return true;
				}
			}

			if (candidate.allowModal && hovered)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis] || !ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					beginDrag();
					windowState.sceneArrowGizmoDragging = true;
					windowState.sceneArrowGizmoModalDrag = true;
					windowState.sceneArrowGizmoAxis = axis;
					windowState.sceneArrowGizmoEndpointTarget = candidate.target;
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					windowState.sceneArrowGizmoDragAxisScreenDir = axisScreenDir[axis];
					windowState.sceneArrowGizmoDragAxisWorldDir = kWorldAxes[axis];
					windowState.sceneArrowGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[axis]);
					return true;
				}
			}
		}

		return anyCandidateHovered;
	}

	// Keyboard-only shortcuts for the selected pinned measurement label (`M`, see
	// RendererLayer::onLabelsToggleSelectedBondRequested, pins the current atom selection) - F flip,
	// Delete-to-unpin, Ctrl+Shift+</> scale-step. Also Delete for the selected free label. No mouse
	// hit-test of its own (that's handlePinnedMeasurementInteraction/handleFreeLabelInteraction), so
	// the caller runs this unconditionally every frame rather than folding it into their
	// short-circuiting OR chain.
	void RendererPanel::handlePinnedMeasurementKeyboardShortcuts(RendererWindowState &windowState, bool hovered)
	{
		if (!windowState.pickLabels)
			return;

		// F flips every selected pin's bond-aligned label 180 degrees (item 5) - independent of
		// hover/drag state below since it acts on whatever is already selected, not the cursor.
		// TODO: also expose as a toolbar button once one exists (see toolbar proposal).
		const bool pinSelected = !windowState.selectedPinnedMeasurements.empty();
		if (pinSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
			{
				if (pinIndex < windowState.pinnedMeasurements.size())
					windowState.pinnedMeasurements[pinIndex].flipped ^= true;
			}
		}
		// Delete removes every selected pin - M/Shift+M are add-only (a bulk press over a growing
		// selection used to also silently unpin anything already pinned within it, which made
		// "select more, press M again" an unpredictable mix of adding and removing), so this is now
		// the only way to unpin a label; click (or box/circle-select) to select it/them first.
		if (pinSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			// Descending order so earlier erases don't invalidate the indices still queued below.
			std::vector<std::size_t> sortedSelection = windowState.selectedPinnedMeasurements;
			std::sort(sortedSelection.begin(), sortedSelection.end(), std::greater<>());
			for (const std::size_t pinIndex : sortedSelection)
			{
				if (pinIndex < windowState.pinnedMeasurements.size())
					windowState.pinnedMeasurements.erase(windowState.pinnedMeasurements.begin() + pinIndex);
			}
			windowState.selectedPinnedMeasurements.clear();
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}
		// Ctrl+Shift+>/< steps every selected pin's size (the same RendererWindowState::
		// PinnedMeasurement::scale the label transform gizmo's Scale handle drags) by a fixed
		// increment - a quicker alternative to dragging that handle when only a nudge is needed. > / <
		// are Shift+Period/Shift+Comma on a standard layout, so this checks the base keys plus
		// KeyShift explicitly rather than relying on ImGui to resolve the shifted glyph.
		if (pinSelected && hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift)
		{
			constexpr float kScaleStep = 0.1f;
			constexpr float kMinScale = 0.2f;
			constexpr float kMaxScale = 5.0f;
			if (ImGui::IsKeyPressed(ImGuiKey_Period, false))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
				{
					if (pinIndex < windowState.pinnedMeasurements.size())
					{
						float &scale = windowState.pinnedMeasurements[pinIndex].style.scale;
						scale = std::min(kMaxScale, scale + kScaleStep);
					}
				}
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Comma, false))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
				{
					if (pinIndex < windowState.pinnedMeasurements.size())
					{
						float &scale = windowState.pinnedMeasurements[pinIndex].style.scale;
						scale = std::max(kMinScale, scale - kScaleStep);
					}
				}
			}
		}

		// Delete removes every selected free label - same rationale as the pin Delete above.
		const bool freeLabelSelected = !windowState.selectedFreeLabels.empty();
		if (freeLabelSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			std::vector<std::size_t> sortedSelection = windowState.selectedFreeLabels;
			std::sort(sortedSelection.begin(), sortedSelection.end(), std::greater<>());
			for (const std::size_t labelIndex : sortedSelection)
			{
				if (labelIndex < windowState.freeLabels.size())
					windowState.freeLabels.erase(windowState.freeLabels.begin() + labelIndex);
			}
			windowState.selectedFreeLabels.clear();
		}

		// Delete removes every selected scene arrow - same rationale as the pin/free-label Delete above.
		const bool sceneArrowSelected = !windowState.selectedSceneArrows.empty();
		if (sceneArrowSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			EraseSceneArrows(windowState, windowState.selectedSceneArrows);
		}

		// Ctrl+C/V/D for scene arrows - same "raw ImGui key check, bypass CoreLayer entirely" shape as
		// Delete just above. renderer.selection.copy/paste/duplicate (CoreLayer-dispatched, bound to the
		// same chords) only ever touch atoms - there's no fallback chain in CoreLayer::dispatchKeyChord
		// to make them "also try arrows", so this runs independently, same as Delete already does across
		// every RendererWindowState-only kind (pins/free labels/arrows) alongside the atom command.
		const bool ctrlHeld = ImGui::GetIO().KeyCtrl;
		if (sceneArrowSelected && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
			CopySceneArrowsToClipboard(windowState);
		if (hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_V, false))
			PasteSceneArrowsFromClipboard(windowState); // no selection required, mirrors atom Paste
		if (sceneArrowSelected && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_D, false))
			DuplicateSelectedSceneArrows(windowState);

		// Tab cycles which single point (Start -> End -> whole arrow) owns
		// renderSceneArrowTransformGizmo's drawn/hit-tested axis-triad - keyboard equivalent of clicking
		// the plain dots it draws for the inactive candidates (item 3 of the prior feedback round).
		const bool oneArrowSelected = windowState.selectedSceneArrows.size() == 1;
		if (oneArrowSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
		{
			using DragTarget = RendererWindowState::SceneArrowDragTarget;
			windowState.sceneArrowGizmoActiveTarget = windowState.sceneArrowGizmoActiveTarget == DragTarget::Both
				? DragTarget::Start
				: windowState.sceneArrowGizmoActiveTarget == DragTarget::Start ? DragTarget::End : DragTarget::Both;
		}

		// Geometry/Style copy-paste shortcuts - keyboard equivalents of the viewport context menu's
		// "Arrow > Copy/Paste Geometry|Style" items (same GetArrowGeometryClipboard/GetArrowStyleClipboard
		// pair, see that menu for why two independent slots instead of one tagged one). Paste Style would
		// naturally be Alt+V to mirror Copy Style's Alt+C, but Alt+V is already renderer.view.cycle_previous
		// (keybindings.yaml) - Alt+Shift+V instead. Likewise the whole-arrow Ctrl+C/V/D block above already
		// owns plain Ctrl+C/V, so these are Ctrl+Shift+C/V.
		const bool altHeld = ImGui::GetIO().KeyAlt;
		const bool shiftHeld = ImGui::GetIO().KeyShift;
		if (sceneArrowSelected && hovered && ctrlHeld && shiftHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
			CopyArrowGeometry(windowState.sceneArrows[windowState.selectedSceneArrows.front()].style);
		if (sceneArrowSelected && hovered && ctrlHeld && shiftHeld && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
			GetArrowGeometryClipboard().has_value())
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			PasteArrowGeometry(windowState, windowState.selectedSceneArrows);
		}
		if (sceneArrowSelected && hovered && altHeld && !shiftHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
			CopyArrowStyle(windowState.sceneArrows[windowState.selectedSceneArrows.front()].style);
		if (sceneArrowSelected && hovered && altHeld && shiftHeld && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
			GetArrowStyleClipboard().has_value())
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			PasteArrowStyle(windowState, windowState.selectedSceneArrows);
		}

		// Shift+R toggles the Rotate pivot (Midpoint <-> 3D Cursor) - keyboard equivalent of the toolbar
		// pivot-mode button (RendererPanelToolbar.cpp), only meaningful while an arrow is selected and
		// Rotate is the active gizmo mode, same visibility condition that toolbar button uses.
		if (sceneArrowSelected && hovered && windowState.gizmoOperation == GizmoOperation::Rotate && shiftHeld &&
			ImGui::IsKeyPressed(ImGuiKey_R, false))
		{
			using PivotMode = RendererWindowState::ArrowGizmoPivotMode;
			windowState.sceneArrowGizmoPivotMode = windowState.sceneArrowGizmoPivotMode == PivotMode::Midpoint
				? PivotMode::Cursor3D
				: PivotMode::Midpoint;
		}

		// Ctrl+Home sets the 3D cursor to the selected arrow's currently active gizmo point (Start/End/
		// midpoint per sceneArrowGizmoActiveTarget) - keyboard equivalent of the context menu's "3D Cursor >
		// Move to Arrow Start/End", extended to also cover the midpoint. Plain Home is already
		// renderer.orbit_left_90 (keybindings.yaml), hence Ctrl+Home instead.
		if (oneArrowSelected && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Home, false))
		{
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus != nullptr)
			{
				using DragTarget = RendererWindowState::SceneArrowDragTarget;
				const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[windowState.selectedSceneArrows.front()];
				const glm::vec3 position = windowState.sceneArrowGizmoActiveTarget == DragTarget::Start ? arrow.start
					: windowState.sceneArrowGizmoActiveTarget == DragTarget::End                        ? arrow.end
																										  : (arrow.start + arrow.end) * 0.5f;
				RendererEvents::Viewport::Cursor3DSetPositionRequested event;
				event.windowId = windowState.windowId;
				event.position = position;
				eventBus->Publish(event);
			}
		}
	}

	// Click-select + drag-to-nudge for pinned measurement labels only (mouse path) - the keyboard
	// shortcuts that used to live in this function moved to handlePinnedMeasurementKeyboardShortcuts
	// above, which the caller runs unconditionally every frame; this half still short-circuits behind
	// gizmoCapturing (see Render()) so a click already claimed by the atom/label transform gizmo can't
	// also be reinterpreted here as a pin pick/drag-start.
	bool RendererPanel::handlePinnedMeasurementInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels)
			return false;

		if (windowState.camera == nullptr)
			return false;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
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

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);

		// Resolves the same anchor point renderLabels() draws the pin at (bond midpoint / angle
		// vertex), offset already applied - ignores the bond's periodic-image shift for a 2-atom
		// pin (only matters for bonds crossing a periodic cell boundary), fine for a hit-test.
		auto resolveAnchor = [&](const RendererWindowState::PinnedMeasurement &pin, glm::vec3 &outAnchor) -> bool {
			const bool inRange = std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(), [&](const std::size_t index) {
				return index < windowState.structure.atoms.size();
			});
			if (!inRange)
				return false;
			if (pin.atomIndices.size() == 2)
			{
				outAnchor = (windowState.structure.atoms[pin.atomIndices[0]].cartesianPosition +
								windowState.structure.atoms[pin.atomIndices[1]].cartesianPosition) *
					0.5f;
			}
			else if (pin.atomIndices.size() == 3)
			{
				const std::size_t vertexIndex = ResolveAngleVertexIndex(windowState.structure, pin.atomIndices);
				outAnchor = windowState.structure.atoms[vertexIndex].cartesianPosition;
			}
			else
			{
				return false;
			}
			outAnchor += pin.worldOffset;
			return true;
		};

		if (windowState.pinnedMeasurementDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || windowState.selectedPinnedMeasurements.empty())
			{
				windowState.pinnedMeasurementDragging = false;
				return false;
			}

			// Uses the most-recently-selected pin (back()) purely as the reference point for
			// converting screen-pixel mouse movement into a world-space delta - the SAME resulting
			// delta then applies to every selected pin's worldOffset below (rigid group drag).
			const std::size_t referenceIndex = windowState.selectedPinnedMeasurements.back();
			if (referenceIndex >= windowState.pinnedMeasurements.size())
			{
				windowState.pinnedMeasurementDragging = false;
				return false;
			}
			glm::vec3 anchor(0.0f);
			if (resolveAnchor(windowState.pinnedMeasurements[referenceIndex], anchor))
			{
				glm::vec2 rightProbe, upProbe, anchorScreen;
				if (projectToScreen(anchor, anchorScreen) && projectToScreen(anchor + cameraRight, rightProbe) &&
					projectToScreen(anchor + cameraUp, upProbe))
				{
					const float pixelsPerWorldRight = std::max(1.0f, glm::length(rightProbe - anchorScreen));
					const float pixelsPerWorldUp = std::max(1.0f, glm::length(upProbe - anchorScreen));
					const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
					const glm::vec2 deltaPixels = mousePos - windowState.pinnedMeasurementDragLastMouse;
					// screen Y is flipped vs cameraUp - same convention as the gizmo axis drag below.
					const glm::vec3 worldDelta = cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
						cameraUp * (deltaPixels.y / pixelsPerWorldUp);
					for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
					{
						if (pinIndex < windowState.pinnedMeasurements.size())
							windowState.pinnedMeasurements[pinIndex].worldOffset += worldDelta;
					}
					windowState.pinnedMeasurementDragLastMouse = mousePos;
				}
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const bool additive = ImGui::GetIO().KeyCtrl;
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		// An unpinned/undragged label's anchor sits exactly at its bond's midpoint (or angle's
		// vertex) - this pick test runs before handleViewportPick's own atom/bond hit-test and
		// unconditionally consumes the click if it hits (see gizmoCapturing in Render()), so a
		// generous radius here made every bond carrying a label nearly impossible to click as a bond:
		// any click within it always won the label instead, however much closer the bond itself was.
		// Was 40px; still comfortably clickable but no longer swallows most of the bond around it.
		constexpr float kPickRadius = 16.0f;
		int hitIndex = -1;
		float bestDistance = kPickRadius;
		for (std::size_t i = 0; i < windowState.pinnedMeasurements.size(); ++i)
		{
			glm::vec3 anchor(0.0f);
			glm::vec2 anchorScreen;
			if (!resolveAnchor(windowState.pinnedMeasurements[i], anchor) || !projectToScreen(anchor, anchorScreen))
				continue;
			const float distance = glm::length(mousePos - anchorScreen);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				hitIndex = static_cast<int>(i);
			}
		}

		std::vector<std::size_t> &selection = windowState.selectedPinnedMeasurements;
		if (hitIndex < 0)
		{
			// Ctrl+click on empty space is a no-op (matches handleAtomPick's additive convention) -
			// only a plain click clears the selection.
			if (!additive)
				selection.clear();
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return false;
		}

		const std::size_t hitPin = static_cast<std::size_t>(hitIndex);
		const auto existing = std::find(selection.begin(), selection.end(), hitPin);
		// Mutual exclusivity with free-label/arrow selection below - all three live in the same OR
		// chain in Render() and short-circuit, so a pin hit here would otherwise leave a stale
		// free-label/arrow selection in place instead of replacing it the way clicking a different
		// pin already does.
		windowState.selectedFreeLabels.clear();
		windowState.selectedSceneArrows.clear();

		if (additive)
		{
			// Ctrl+click toggles selection (same convention as handleAtomPick) but never starts a
			// drag on its own - it's a pure select/deselect gesture, same as Blender/most DCC tools;
			// dragging the resulting group needs its own separate plain click-and-hold afterward.
			if (existing != selection.end())
				selection.erase(existing);
			else
				selection.push_back(hitPin);
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return true;
		}

		if (existing == selection.end())
		{
			// Plain click on an UNSELECTED pin replaces the whole selection. Plain click on one
			// that's already part of a multi-selection leaves the group as-is - same "click-and-drag
			// the group you already have" convention most DCC tools use, so a multi-select doesn't
			// collapse to one item just from grabbing it.
			selection.clear();
			selection.push_back(hitPin);
		}
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.pinnedMeasurementDragging = true;
		windowState.pinnedMeasurementDragLastMouse = mousePos;
		return true;
	}

	// Click-select + drag-along-camera-plane for freeLabels - same shape as
	// handlePinnedMeasurementInteraction's mouse half above, simplified: a free label's own
	// worldPosition IS the anchor (no bond/angle to resolve, no periodic offset), so this drags that
	// field directly instead of a separate worldOffset.
	bool RendererPanel::handleFreeLabelInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr)
			return false;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
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

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);

		if (windowState.freeLabelDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || windowState.selectedFreeLabels.empty())
			{
				windowState.freeLabelDragging = false;
				return false;
			}

			// Reference point for the pixel->world conversion only - the resulting delta applies to
			// every selected free label below (rigid group drag), same convention as the pinned
			// measurement drag above.
			const std::size_t referenceIndex = windowState.selectedFreeLabels.back();
			if (referenceIndex >= windowState.freeLabels.size())
			{
				windowState.freeLabelDragging = false;
				return false;
			}
			const glm::vec3 referencePosition = windowState.freeLabels[referenceIndex].worldPosition;
			glm::vec2 anchorScreen, rightProbe, upProbe;
			if (projectToScreen(referencePosition, anchorScreen) &&
				projectToScreen(referencePosition + cameraRight, rightProbe) &&
				projectToScreen(referencePosition + cameraUp, upProbe))
			{
				const float pixelsPerWorldRight = std::max(1.0f, glm::length(rightProbe - anchorScreen));
				const float pixelsPerWorldUp = std::max(1.0f, glm::length(upProbe - anchorScreen));
				const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
				const glm::vec2 deltaPixels = mousePos - windowState.freeLabelDragLastMouse;
				const glm::vec3 worldDelta = cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
					cameraUp * (deltaPixels.y / pixelsPerWorldUp);
				for (const std::size_t labelIndex : windowState.selectedFreeLabels)
				{
					if (labelIndex < windowState.freeLabels.size())
						windowState.freeLabels[labelIndex].worldPosition += worldDelta;
				}
				windowState.freeLabelDragLastMouse = mousePos;
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const bool additive = ImGui::GetIO().KeyCtrl;
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		constexpr float kPickRadius = 16.0f;
		int hitIndex = -1;
		float bestDistance = kPickRadius;
		for (std::size_t i = 0; i < windowState.freeLabels.size(); ++i)
		{
			glm::vec2 labelScreen;
			if (!projectToScreen(windowState.freeLabels[i].worldPosition, labelScreen))
				continue;
			const float distance = glm::length(mousePos - labelScreen);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				hitIndex = static_cast<int>(i);
			}
		}

		std::vector<std::size_t> &selection = windowState.selectedFreeLabels;
		if (hitIndex < 0)
		{
			if (!additive)
				selection.clear();
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return false;
		}

		const std::size_t hitLabel = static_cast<std::size_t>(hitIndex);
		const auto existing = std::find(selection.begin(), selection.end(), hitLabel);
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedSceneArrows.clear();

		if (additive)
		{
			if (existing != selection.end())
				selection.erase(existing);
			else
				selection.push_back(hitLabel);
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return true;
		}

		if (existing == selection.end())
		{
			selection.clear();
			selection.push_back(hitLabel);
		}
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.freeLabelDragging = true;
		windowState.freeLabelDragLastMouse = mousePos;
		return true;
	}

	// Click-select + drag for sceneArrows - same click/Ctrl-toggle/drag shape as
	// handleFreeLabelInteraction above, but the hit-test is against a SEGMENT (start->end), not a
	// single anchor point, and a single selected arrow's drag moves only whichever endpoint was
	// actually grabbed (screen-space proximity at click time decides that, no drawn gizmo widget
	// needed - same idea as isBondUnderScreenPosition's proximity band, just resolved once instead
	// of every frame). Multiple selected arrows always move rigidly together (every selected arrow's
	// start AND end shift by the same delta), same group-drag convention as labels.
	bool RendererPanel::handleSceneArrowInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr)
			return false;

		using SceneArrow = RendererWindowState::SceneArrow;
		using DragTarget = RendererWindowState::SceneArrowDragTarget;
		using ArrowKind = RendererWindowState::ArrowKind;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
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

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);

		if (windowState.sceneArrowDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || windowState.selectedSceneArrows.empty())
			{
				windowState.sceneArrowDragging = false;
				return false;
			}

			const std::size_t referenceIndex = windowState.selectedSceneArrows.back();
			if (referenceIndex >= windowState.sceneArrows.size())
			{
				windowState.sceneArrowDragging = false;
				return false;
			}
			const bool singleSelection = windowState.selectedSceneArrows.size() == 1;
			const SceneArrow &referenceArrow = windowState.sceneArrows[referenceIndex];
			glm::vec3 referencePosition = (referenceArrow.start + referenceArrow.end) * 0.5f;
			if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::Start)
				referencePosition = referenceArrow.start;
			else if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::End)
				referencePosition = referenceArrow.end;

			glm::vec2 anchorScreen, rightProbe, upProbe;
			if (projectToScreen(referencePosition, anchorScreen) &&
				projectToScreen(referencePosition + cameraRight, rightProbe) &&
				projectToScreen(referencePosition + cameraUp, upProbe))
			{
				const float pixelsPerWorldRight = std::max(1.0f, glm::length(rightProbe - anchorScreen));
				const float pixelsPerWorldUp = std::max(1.0f, glm::length(upProbe - anchorScreen));
				const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
				const glm::vec2 deltaPixels = mousePos - windowState.sceneArrowDragLastMouse;
				const glm::vec3 worldDelta = cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
					cameraUp * (deltaPixels.y / pixelsPerWorldUp);
				for (const std::size_t arrowIndex : windowState.selectedSceneArrows)
				{
					if (arrowIndex >= windowState.sceneArrows.size())
						continue;
					SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
					if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::Start)
						arrow.start += worldDelta;
					else if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::End)
						arrow.end += worldDelta;
					else
					{
						arrow.start += worldDelta;
						arrow.end += worldDelta;
					}
				}
				windowState.sceneArrowDragLastMouse = mousePos;
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const bool additive = ImGui::GetIO().KeyCtrl;
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		// Tolerance scales with each arrow's actual rendered width instead of a flat radius (docs/
		// scene_arrow_rework_plan_corrected.md Step 9) - a thick arrow should be easier to click than
		// a thin one, and the shape actually drawn (triangle head) should be what gets picked. Arrow2D's
		// shaftWidth/headWidth/headLength/outlineWidth are already screen-space pixels (Section 8), so
		// they're used as-is; Arrow3D/Line are world-space and need a per-arrow world->pixel probe -
		// same camera-right projection trick the drag code above uses for pixelsPerWorldRight/Up.
		int hitIndex = -1;
		float bestDistance = std::numeric_limits<float>::max();
		glm::vec2 hitScreenStart(0.0f), hitScreenEnd(0.0f);
		float hitShaftHalfPx = 0.0f;
		for (std::size_t i = 0; i < windowState.sceneArrows.size(); ++i)
		{
			const SceneArrow &candidate = windowState.sceneArrows[i];
			glm::vec2 screenStart, screenEnd;
			if (!projectToScreen(candidate.start, screenStart) || !projectToScreen(candidate.end, screenEnd))
				continue;

			const RendererWindowState::ArrowStyle &style = candidate.style;
			const bool isArrow2D = candidate.kind == ArrowKind::Arrow2D;
			float pixelsPerWorld = 1.0f;
			if (!isArrow2D)
			{
				const glm::vec3 midWorld = (candidate.start + candidate.end) * 0.5f;
				glm::vec2 screenMid, rightProbe;
				if (projectToScreen(midWorld, screenMid) && projectToScreen(midWorld + cameraRight, rightProbe))
					pixelsPerWorld = std::max(glm::length(rightProbe - screenMid), 0.0001f);
			}
			const float shaftHalfPx = isArrow2D ? style.shaftWidth * 0.5f : style.shaftWidth * 0.5f * pixelsPerWorld;
			const float outlinePx = isArrow2D ? style.outlineWidth : 0.0f;
			const float shaftTolerance = std::max(12.0f, shaftHalfPx + outlinePx + 4.0f);
			const float shaftDistance = SelectionHitTest::DistancePointToSegment(mousePos, screenStart, screenEnd);
			float bestForCandidate =
				shaftDistance <= shaftTolerance ? shaftDistance : std::numeric_limits<float>::max();

			const glm::vec2 screenAxis = screenEnd - screenStart;
			const float screenLength = glm::length(screenAxis);
			const glm::vec2 dirScreen = screenLength > 0.0001f ? screenAxis / screenLength : glm::vec2(1.0f, 0.0f);
			if (isArrow2D)
			{
				// Mirrors renderSceneArrows' Arrow2D head triangle exactly (same clamp too), so the
				// pickable area matches the visible shape instead of a generic radius around the tip.
				const glm::vec2 perpScreen(-dirScreen.y, dirScreen.x);
				const float headLengthPx = std::min(style.headLength, 0.45f * screenLength);
				const glm::vec2 headBase = screenEnd - dirScreen * headLengthPx;
				const float headDistance = SelectionHitTest::DistancePointToTriangle2D(
					mousePos, headBase + perpScreen * (style.headWidth * 0.5f),
					headBase - perpScreen * (style.headWidth * 0.5f), screenEnd);
				if (headDistance <= 4.0f)
					bestForCandidate = std::min(bestForCandidate, headDistance);
			}
			else if (candidate.kind == ArrowKind::Arrow3D)
			{
				const float headRadiusPx = style.headWidth * 0.5f * pixelsPerWorld;
				const float headTolerance = std::max(14.0f, headRadiusPx);
				const float headDistance = glm::length(mousePos - screenEnd);
				if (headDistance <= headTolerance)
					bestForCandidate = std::min(bestForCandidate, headDistance);
			}
			// Line: no head test (doc Step 9).

			if (bestForCandidate < bestDistance)
			{
				bestDistance = bestForCandidate;
				hitIndex = static_cast<int>(i);
				hitScreenStart = screenStart;
				hitScreenEnd = screenEnd;
				hitShaftHalfPx = shaftHalfPx;
			}
		}

		std::vector<std::size_t> &selection = windowState.selectedSceneArrows;
		if (hitIndex < 0)
		{
			if (!additive)
				selection.clear();
			return false;
		}

		const std::size_t hitArrow = static_cast<std::size_t>(hitIndex);
		const auto existing = std::find(selection.begin(), selection.end(), hitArrow);
		// Mutual exclusivity with label selection, same convention as pin/free-label clicks above.
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedFreeLabels.clear();

		if (additive)
		{
			if (existing != selection.end())
				selection.erase(existing);
			else
				selection.push_back(hitArrow);
			return true;
		}

		if (existing == selection.end())
		{
			selection.clear();
			selection.push_back(hitArrow);
		}

		// Which endpoint this click actually grabbed - only matters once the selection is (or
		// becomes) exactly this one arrow; a multi-selection drag always moves every selected
		// arrow's start AND end together regardless of this. Scales with the hit arrow's own shaft
		// half-width, same reasoning as the shaft/head tolerances above (doc Step 9).
		const float endpointTolerance = std::max(14.0f, hitShaftHalfPx + 8.0f);
		const float distanceToStart = glm::length(mousePos - hitScreenStart);
		const float distanceToEnd = glm::length(mousePos - hitScreenEnd);
		if (distanceToStart <= endpointTolerance && distanceToStart <= distanceToEnd)
			windowState.sceneArrowDragTarget = DragTarget::Start;
		else if (distanceToEnd <= endpointTolerance)
			windowState.sceneArrowDragTarget = DragTarget::End;
		else
			windowState.sceneArrowDragTarget = DragTarget::Both;

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.sceneArrowDragging = true;
		windowState.sceneArrowDragLastMouse = mousePos;
		return true;
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

	std::vector<std::size_t> RendererPanel::hitTestRectBonds(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const
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

	std::vector<std::size_t> RendererPanel::hitTestCircleBonds(
		const RendererWindowState &windowState, glm::vec2 center, float radius) const
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

	// Label counterparts of hitTestRect/hitTestCircle above - same "return every match" shape now
	// that label selection is multi-select too (selectedPinnedMeasurements/selectedFreeLabels).
	std::vector<std::size_t> RendererPanel::hitTestRectPinnedMeasurements(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const
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

	std::vector<std::size_t> RendererPanel::hitTestCirclePinnedMeasurements(
		const RendererWindowState &windowState, glm::vec2 center, float radius) const
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

	std::vector<std::size_t> RendererPanel::hitTestRectFreeLabels(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const
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

	std::vector<std::size_t> RendererPanel::hitTestCircleFreeLabels(
		const RendererWindowState &windowState, glm::vec2 center, float radius) const
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
	std::vector<std::size_t> RendererPanel::hitTestRectSceneArrows(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const
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

	std::vector<std::size_t> RendererPanel::hitTestCircleSceneArrows(
		const RendererWindowState &windowState, glm::vec2 center, float radius) const
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
	void RendererPanel::applyLabelRegionSelection(
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
		std::vector<std::size_t> bondIndices,
		RendererEvents::Viewport::RegionSelectMode mode)
	{
		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return;
		RendererEvents::Viewport::RegionSelectionRequested event;
		event.windowId = windowState.windowId;
		event.atomIndices = std::move(atomIndices);
		event.bondIndices = std::move(bondIndices);
		event.mode = mode;
		eventBus->Publish(event);
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
