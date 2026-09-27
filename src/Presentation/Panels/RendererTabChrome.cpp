#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererTabChrome.hpp"

#include <algorithm>
#include <string>

#include <imgui_internal.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/ViewportToolbars.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr std::string_view kRendererWindowPrefix = "RendererWindow_";
		constexpr const char *kCloseConfirmPopupId = "Close renderer tab?";

		[[nodiscard]] RendererWindowState *FindWindow(RendererLayer &layer, const std::string &windowId)
		{
			for (RendererWindowState &windowState : layer.GetWindows())
			{
				if (windowState.windowId == windowId)
					return &windowState;
			}
			return nullptr;
		}

		[[nodiscard]] float DrawHorizontalToolbarOverlay(
			RendererWindowState &windowState,
			RendererLayer &layer,
			const ImVec2 viewportOrigin,
			const ImVec2 viewportSize)
		{
			// Width is explicit on the outer child, while the toolbar's own BeginChild asks for width 0,
			// meaning "fill the window". The outer child lets only the height auto-size, so the toolbar
			// has a stable width to fill.
			ImGui::SetCursorScreenPos(viewportOrigin);
			if (!ImGui::BeginChild(
					"##RendererViewportToolbarOverlay",
					ImVec2(viewportSize.x, 0.0f),
					ImGuiChildFlags_AutoResizeY,
					ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
			{
				ImGui::EndChild();
				return 0.0f;
			}

			DrawViewportToolbar(windowState, layer);
			const float height = ImGui::GetWindowSize().y;
			ImGui::EndChild();
			return height;
		}

		void DrawVerticalToolbarOverlay(
			RendererWindowState &windowState,
			RendererLayer &layer,
			const ImVec2 viewportOrigin,
			const ImVec2 viewportSize,
			const float horizontalHeight)
		{
			// The mirror image of the horizontal overlay. This toolbar's BeginChild asks for an
			// explicit width and GetContentRegionAvail().y for its height, so here the HEIGHT is the
			// pinned side and the width is left to auto-size.
			//
			// Deliberately no second copy of the column-width formula: it lives in
			// ViewportVerticalToolbar.cpp, and two formulas for one width would drift apart the first
			// time either one is touched.
			const float margin = ImGui::GetStyle().WindowPadding.y * 2.0f;
			const float height = std::max(0.0f, viewportSize.y - horizontalHeight - margin);
			if (height <= 0.0f)
				return;

			ImGui::SetCursorScreenPos(ImVec2(viewportOrigin.x, viewportOrigin.y + horizontalHeight + margin));
			if (!ImGui::BeginChild(
					"##RendererViewportVerticalToolbarOverlay",
					ImVec2(0.0f, height),
					ImGuiChildFlags_AutoResizeX,
					ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
			{
				ImGui::EndChild();
				return;
			}

			DrawViewportVerticalToolbar(windowState, layer);
			ImGui::EndChild();
		}

		[[nodiscard]] bool CentralNodeHasRendererWindow(const ImGuiDockNode &node)
		{
			for (ImGuiWindow *window : node.Windows)
			{
				if (window != nullptr && !ParseRendererWindowId(window->Name).empty())
					return true;
			}
			return false;
		}

	} // namespace

	std::string ParseRendererWindowId(const std::string_view imguiWindowName)
	{
		const std::size_t marker = imguiWindowName.rfind("###");
		if (marker == std::string_view::npos)
			return {};

		const std::string_view identity = imguiWindowName.substr(marker + 3);
		if (!identity.starts_with(kRendererWindowPrefix))
			return {};

		const std::string_view windowId = identity.substr(kRendererWindowPrefix.size());
		return windowId.empty() ? std::string{} : std::string(windowId);
	}

	std::string ResolveActiveRendererWindowId(RendererLayer &layer)
	{
		if (ImGui::GetMainViewport() != nullptr)
		{
			if (ImGuiDockNode *centralNode = ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID))
			{
				if (centralNode->VisibleWindow != nullptr)
				{
					if (const std::string windowId = ParseRendererWindowId(centralNode->VisibleWindow->Name);
						!windowId.empty())
						return windowId;
				}
			}
		}

		return layer.GetLastFocusedViewportWindowId();
	}

	void DrawViewportToolbarOverlays(
		RendererWindowState &windowState,
		RendererLayer &layer,
		const ImVec2 viewportOrigin,
		const ImVec2 viewportSize)
	{
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		const float horizontalHeight =
			DrawHorizontalToolbarOverlay(windowState, layer, viewportOrigin, viewportSize);
		DrawVerticalToolbarOverlay(windowState, layer, viewportOrigin, viewportSize, horizontalHeight);
	}

	void DrawRendererTabBarAddButton(RendererLayer &layer, EventBus &eventBus)
	{
		if (ImGui::GetMainViewport() == nullptr)
			return;

		ImGuiDockNode *centralNode = ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID);
		if (centralNode == nullptr || !CentralNodeHasRendererWindow(*centralNode))
			return;

		if (!ImGui::DockNodeBeginAmendTabBar(centralNode))
			return;

		if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
		{
			RendererEvents::Windows::OpenEmptyRequested event;
			eventBus.Publish(event);
		}
		ImGui::DockNodeEndAmendTabBar();
		(void)layer;
	}

	bool RendererTabHoldsContent(const RendererWindowState &windowState)
	{
		return !windowState.structure.domainStructureId.empty() ||
			!windowState.structure.atoms.empty() ||
			!windowState.pinnedMeasurements.empty() ||
			!windowState.freeLabels.empty() ||
			!windowState.sceneArrows.empty() ||
			!windowState.sceneOrbitals.empty() ||
			!windowState.scenePlanes.empty() ||
			(windowState.paths != nullptr && !windowState.paths->Store().Empty());
	}

	void RendererTabCloseCoordinator::Request(const std::string &windowId)
	{
		if (m_Resolved || m_ConfirmOpen)
			return;

		m_PendingWindowId = windowId;
		m_Resolved = true;
	}

	void RendererTabCloseCoordinator::Drain(RendererLayer &layer, std::vector<std::string> &windowsToClose)
	{
		const auto clearPendingClose = [this]()
		{
			m_PendingWindowId.clear();
			m_Resolved = false;
			m_ConfirmOpen = false;
		};

		if (m_ConfirmOpen)
		{
			// The state is cleared off IsPopupOpen rather than off BeginPopupModal's return value.
			// Escape closes an ImGui modal without going through either button, and then
			// BeginPopupModal simply returns false with its p_open untouched - so a branch that only
			// clears on a button or on p_open leaves m_ConfirmOpen latched and Ctrl+W dead for the
			// rest of the session.
			if (!ImGui::IsPopupOpen(kCloseConfirmPopupId))
			{
				clearPendingClose();
				return;
			}

			if (ImGui::BeginPopupModal(kCloseConfirmPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextUnformatted("This tab holds scene content. Close it?");
				if (ImGui::Button("Close"))
				{
					windowsToClose.push_back(m_PendingWindowId);
					ImGui::CloseCurrentPopup();
					clearPendingClose();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
					clearPendingClose();
				}
				ImGui::EndPopup();
			}
			return;
		}

		if (!m_Resolved)
			return;

		const std::string windowId =
			m_PendingWindowId.empty() ? ResolveActiveRendererWindowId(layer) : m_PendingWindowId;
		RendererWindowState *windowState = FindWindow(layer, windowId);
		if (windowState == nullptr)
		{
			clearPendingClose();
			return;
		}

		m_PendingWindowId = windowId;
		if (!RendererTabHoldsContent(*windowState))
		{
			windowsToClose.push_back(windowId);
			clearPendingClose();
			return;
		}

		m_ConfirmOpen = true;
		ImGui::OpenPopup(kCloseConfirmPopupId);
	}
} // namespace DefectStudio
