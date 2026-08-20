#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::DockBuilderGetCentralNode - auto-dock new windows into it

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
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
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_EventBus(std::move(eventBus)),
		  m_ContextManager(std::move(contextManager))
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

		if (hovered)
			applyViewportInputNavigation(windowState, imageOrigin, deltaTime);
		else
		{
			windowState.dragActive = false;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				m_Layer.CommitViewInteraction(windowState.windowId);
			}
		}

		if (windowState.activeSelectionTool == SelectionToolMode::Box)
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

	// Circle-select is a live brush, not a drag-defined shape: a plain click selects once at the
	// click position (replace), Shift held while the mouse button is down paints continuously
	// (add) as the brush follows the cursor, Ctrl held paints continuously (subtract).
	void RendererPanel::handleCircleSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered)
	{
		if (!hovered)
			return;

		const ImVec2 mousePos = ImGui::GetMousePos();
		const glm::vec2 center(mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y);
		ImGuiIO &io = ImGui::GetIO();

		if (io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			publishRegionSelection(
				windowState,
				hitTestCircle(windowState, center, windowState.circleSelectRadius),
				RendererEvents::Viewport::RegionSelectMode::Add);
			return;
		}
		if (io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			publishRegionSelection(
				windowState,
				hitTestCircle(windowState, center, windowState.circleSelectRadius),
				RendererEvents::Viewport::RegionSelectMode::Subtract);
			return;
		}
		if (!io.KeyShift && !io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			publishRegionSelection(
				windowState,
				hitTestCircle(windowState, center, windowState.circleSelectRadius),
				RendererEvents::Viewport::RegionSelectMode::Replace);
		}
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
