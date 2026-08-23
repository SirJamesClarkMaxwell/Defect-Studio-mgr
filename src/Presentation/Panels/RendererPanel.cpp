#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

		// Closest points between an infinite ray (rayOrigin + t*rayDir, rayDir normalized) and a
		// finite segment [segA, segB] - standard two-line least-squares solve (see e.g. Ericson's
		// "Real-Time Collision Detection" ClosestPtSegmentSegment), with the segment parameter
		// clamped to [0,1] and the ray parameter left unclamped (the caller checks outT > 0 for "in
		// front of camera"). Used by handleViewportPick's bond hit-test - a bond has no analytic ray
		// intersection like a sphere does, so picking it is "is the ray's closest approach to this
		// cylinder's axis within its radius".
		void ClosestPointsRaySegment(
			const glm::vec3 &rayOrigin, const glm::vec3 &rayDir, const glm::vec3 &segA, const glm::vec3 &segB,
			float &outT, glm::vec3 &outClosestOnSegment)
		{
			const glm::vec3 d2 = segB - segA;
			const glm::vec3 w0 = rayOrigin - segA;
			const float e = glm::dot(d2, d2);
			if (e <= 1.0e-8f)
			{
				// Degenerate (zero-length) segment - treat segA as a point.
				outT = glm::dot(segA - rayOrigin, rayDir);
				outClosestOnSegment = segA;
				return;
			}

			const float b = glm::dot(rayDir, d2);
			const float c = glm::dot(rayDir, w0);
			const float f = glm::dot(d2, w0);
			const float denom = e - b * b; // a == dot(rayDir, rayDir) == 1 (normalized)

			float s = denom > 1.0e-8f ? (b * f - c * e) / denom : 0.0f;
			s = std::clamp(s, 0.0f, 1.0f);
			outT = b * s - c;
			outClosestOnSegment = segA + d2 * s;
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
			!windowState.selectionDragActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			windowState.selectedPinnedMeasurement = -1;
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

		const bool gizmoCapturing = renderTransformGizmo(windowState, imageOrigin, viewportSize, hovered) ||
			renderLabelTransformGizmo(windowState, imageOrigin, viewportSize, hovered) ||
			handlePinnedMeasurementInteraction(windowState, imageOrigin, viewportSize, hovered);

		renderViewportContextMenu(windowState, imageOrigin, viewportSize, hovered);

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
				ClosestPointsRaySegment(
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

		if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
			runCommand("renderer.selection.copy");
		if (ImGui::MenuItem("Paste", "Ctrl+V"))
			runCommand("renderer.selection.paste");
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
			runCommand("renderer.selection.duplicate");

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
	bool RendererPanel::renderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.selectedAtomIndices.empty() || windowState.camera == nullptr)
		{
			windowState.gizmoDragActive = false;
			return false;
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
				pivotOnScreen && !atomUnderCursor && radial >= kPickMinDistance && radial <= kPickMaxDistance;

			if (!windowState.fallbackGizmoDragging && hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.fallbackGizmoDragging = true;
				windowState.fallbackGizmoAxis = -2; // sentinel: trackball rotate, not a translate/scale axis
				windowState.fallbackLastMousePos = mousePos;
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

				const bool modalConfirmed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
				if (pivotOnScreen)
				{
					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
					const glm::vec3 lockedAxisWorld = kWorldAxes[windowState.fallbackGizmoAxis];
					// Screen Y is flipped vs standard math convention, and a right-hand rotation
					// around an axis pointing away from the viewer (into the screen) reads as
					// clockwise on-screen - both flips cancel out when the axis points toward the
					// viewer instead, so only one sign check is needed here.
					const float rotationSign = glm::dot(lockedAxisWorld, cameraForward) >= 0.0f ? -1.0f : 1.0f;

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
					windowState.fallbackLastMousePos = mousePos;

					ImDrawList *lockDrawList = ImGui::GetWindowDrawList();
					lockDrawList->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance,
						kAxisLockColors[windowState.fallbackGizmoAxis], 64, 3.0f);
				}
				windowState.gizmoDragActive = true;

				if (modalConfirmed)
				{
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
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
			if (pivotOnScreen && !windowState.fallbackGizmoDragging && !atomUnderCursor)
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
					windowState.gizmoDragActive = false;
					return true;
				}

				// A modal drag (started by X/Y/Z with no button held) applies every frame regardless
				// of mouse-button state and confirms on left-click; a click-drag keeps applying only
				// while the button stays down and commits on release - both fall through to the same
				// apply step below, they just disagree on when "still active" is true.
				const bool modalConfirmed = windowState.fallbackModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
				const bool stillActive =
					windowState.fallbackModalDrag ? !modalConfirmed : ImGui::IsMouseDown(ImGuiMouseButton_Left);

				if (stillActive || modalConfirmed)
				{
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

					if (modalConfirmed)
					{
						windowState.fallbackGizmoDragging = false;
						windowState.fallbackModalDrag = false;
						windowState.fallbackGizmoAxis = -1;
						windowState.fallbackAxisLockOverride = -1;
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

	// Gizmo for the selected pinned measurement label (sibling of renderTransformGizmo above, see
	// PinnedMeasurement::worldOffset/rotationOffsetRadians/scale) - same screen-space pick/drag
	// philosophy as the atom gizmo (ImGuizmo's own picking is unreliable here too, see that
	// function's big comment), just for one point instead of a multi-atom selection. Translate draws
	// the familiar shaft+arrowhead 3-axis handles and moves worldOffset. Rotate/Scale use a single
	// ring-drag around the pivot instead - no per-axis handles, since a camera-facing billboard has
	// only one meaningful rotation axis (its own normal) and one meaningful scale (uniform glyph
	// size), so there is nothing for X/Y/Z to choose between. Every drag pushes one undo snapshot at
	// its start via PushPinnedMeasurementUndoSnapshot (Ctrl+Alt+U/Ctrl+Alt+Shift+U - see
	// RendererEvents::Viewport::UndoLabelsRequested), never mid-drag, so a whole drag is one step.
	// Reads its pivot from the label entity's TransformComponent (kept current by
	// SceneSystem::UpdateLabelTransforms, called once per frame before this).
	bool RendererPanel::renderLabelTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		const bool pinSelected = windowState.selectedPinnedMeasurement >= 0 &&
			windowState.selectedPinnedMeasurement < static_cast<int>(windowState.pinnedMeasurements.size());
		if (!pinSelected || windowState.camera == nullptr)
		{
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoAxis = -1;
			return false;
		}

		Entity labelEntity =
			windowState.sceneRegistry.LabelEntityAt(static_cast<std::size_t>(windowState.selectedPinnedMeasurement));
		if (!labelEntity || !labelEntity.HasComponent<TransformComponent>())
			return false;
		const glm::vec3 pivot = labelEntity.GetComponent<TransformComponent>().position;
		RendererWindowState::PinnedMeasurement &pin =
			windowState.pinnedMeasurements[static_cast<std::size_t>(windowState.selectedPinnedMeasurement)];

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
		constexpr float kPickMinDistance = 14.0f;
		constexpr float kPickMaxDistance = 70.0f;

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
				PushPinnedMeasurementUndoSnapshot(windowState);
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoAxis = -2; // sentinel: ring drag, no X/Y/Z handle
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragStartRotation = pin.rotationOffsetRadians;
				windowState.labelGizmoDragStartScale = pin.scale;
				windowState.labelGizmoDragStartRadial = std::max(kPickMinDistance, radialNow);
			}

			if (windowState.labelGizmoDragging && windowState.labelGizmoAxis == -2)
			{
				if (pivotOnScreen)
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingActiveColor, 48, 3.0f);

				// Cancel: Escape or right-click reverts to the pre-drag snapshot, same convention as
				// the atom gizmo's fallback drag.
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					pin.rotationOffsetRadians = windowState.labelGizmoDragStartRotation;
					pin.scale = windowState.labelGizmoDragStartScale;
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
							pin.rotationOffsetRadians += deltaAngle;
						}
					}
					else
					{
						// Blender S-style: scale ratio is the current radial distance from the pivot
						// over the distance at drag start, not a per-frame delta - dragging back to the
						// start radius always returns exactly to the start scale.
						const float currentRadial = std::max(1.0f, glm::length(mousePos - pivotScreen));
						const float ratio = currentRadial / windowState.labelGizmoDragStartRadial;
						pin.scale = glm::clamp(windowState.labelGizmoDragStartScale * ratio, 0.1f, 8.0f);
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
			constexpr float kArrowHeadLength = 12.0f;
			constexpr float kArrowHeadHalfWidth = 5.0f;
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
			constexpr float kPickPerpTolerance = 12.0f;
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
				PushPinnedMeasurementUndoSnapshot(windowState);
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoAxis = hoveredAxis;
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragAxisScreenDir = axisScreenDir[hoveredAxis];
				windowState.labelGizmoDragAxisWorldDir = kWorldAxes[hoveredAxis];
				windowState.labelGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
				windowState.labelGizmoDragStartOffset = pin.worldOffset;
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

			// Cancel: Escape or right-click reverts to the pre-drag snapshot, same convention as the
			// atom gizmo's fallback drag.
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				pin.worldOffset = windowState.labelGizmoDragStartOffset;
				windowState.labelGizmoDragging = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const glm::vec2 delta = mousePos - windowState.labelGizmoLastMousePos;
				windowState.labelGizmoLastMousePos = mousePos;
				const float deltaOnAxisPixels = glm::dot(delta, windowState.labelGizmoDragAxisScreenDir);
				const float deltaOnAxisWorld = deltaOnAxisPixels / windowState.labelGizmoDragPixelsPerWorld;
				pin.worldOffset += windowState.labelGizmoDragAxisWorldDir * deltaOnAxisWorld;
				return true;
			}

			// Button released - commit (the drag already mutated worldOffset live, nothing further to
			// apply) and consume this one release frame same as the atom gizmo does.
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoAxis = -1;
			return true;
		}

		return hoveredAxis >= 0;
	}

	// Click-select + drag-to-nudge for pinned measurement labels (`M`, see
	// RendererLayer::onLabelsToggleSelectedBondRequested). The gizmo above (renderLabelTransformGizmo)
	// handles precise axis-locked moves via its handles; this is the looser "grab the label glyph
	// itself and drag" path along the camera plane, plus click-to-select and Delete-to-unpin.
	// Returns true if this frame's click/drag was consumed here, so the caller can suppress
	// atom-pick the same way it already does for the transform gizmo (gizmoCapturing).
	bool RendererPanel::handlePinnedMeasurementInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels)
			return false;

		// F flips the selected pin's bond-aligned label 180 degrees (item 5) - independent of
		// hover/drag state below since it acts on whatever is already selected, not the cursor.
		// TODO: also expose as a toolbar button once one exists (see toolbar proposal).
		const bool pinSelected = windowState.selectedPinnedMeasurement >= 0 &&
			windowState.selectedPinnedMeasurement < static_cast<int>(windowState.pinnedMeasurements.size());
		if (pinSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			windowState.pinnedMeasurements[static_cast<std::size_t>(windowState.selectedPinnedMeasurement)].flipped ^= true;
		}
		// Delete removes just the selected pin - M/Shift+M are add-only (a bulk press over a growing
		// selection used to also silently unpin anything already pinned within it, which made
		// "select more, press M again" an unpredictable mix of adding and removing), so this is now
		// the only way to unpin a single label; click a label to select it first.
		if (pinSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			windowState.pinnedMeasurements.erase(
				windowState.pinnedMeasurements.begin() + windowState.selectedPinnedMeasurement);
			windowState.selectedPinnedMeasurement = -1;
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}

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
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
				windowState.selectedPinnedMeasurement < 0 ||
				windowState.selectedPinnedMeasurement >= static_cast<int>(windowState.pinnedMeasurements.size()))
			{
				windowState.pinnedMeasurementDragging = false;
				return false;
			}

			RendererWindowState::PinnedMeasurement &pin =
				windowState.pinnedMeasurements[static_cast<std::size_t>(windowState.selectedPinnedMeasurement)];
			glm::vec3 anchor(0.0f);
			if (resolveAnchor(pin, anchor))
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
					pin.worldOffset += cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
						cameraUp * (deltaPixels.y / pixelsPerWorldUp);
					windowState.pinnedMeasurementDragLastMouse = mousePos;
				}
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		constexpr float kPickRadius = 40.0f;
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

		windowState.selectedPinnedMeasurement = hitIndex;
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		if (hitIndex < 0)
			return false;

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.pinnedMeasurementDragging = true;
		windowState.pinnedMeasurementDragLastMouse = mousePos;
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

		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Apply to selected atoms"))
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				ChangeAtomTypePayload payload;
				payload.windowId = focusedWindowId;
				payload.species = m_Layer.GetSelectedPeriodicElement();
				CommandContext context;
				context.Set<ChangeAtomTypePayload>("atom_edit.change_type_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.selection.change_type"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Change atom type from periodic table failed: {}", result.Error().technicalDetails);
			}
		}
		ImGui::EndDisabled();
		if (!canApply && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Select atoms in a renderer viewport first.");

		ImGui::End();
	}

} // namespace DefectStudio
