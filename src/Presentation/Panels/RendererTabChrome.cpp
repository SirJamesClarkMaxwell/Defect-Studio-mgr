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

		[[nodiscard]] ImGuiWindowFlags OverlayWindowFlags()
		{
			return ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoFocusOnAppearing;
		}

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
			ImGui::SetNextWindowPos(viewportOrigin, ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(viewportSize.x, 0.0f), ImGuiCond_Always);
			if (!ImGui::Begin(
					"##RendererViewportToolbarOverlay", nullptr, OverlayWindowFlags() | ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::End();
				return 0.0f;
			}

			DrawViewportToolbar(windowState, layer);
			const float height = ImGui::GetWindowSize().y;
			ImGui::End();
			return height;
		}

		void DrawVerticalToolbarOverlay(
			RendererWindowState &windowState,
			RendererLayer &layer,
			const ImVec2 viewportOrigin,
			const ImVec2 viewportSize,
			const float horizontalHeight)
		{
			const float uiScale = ImGui::GetIO().FontGlobalScale / kViewportToolbarFontScaleBaseline;
			const float iconExtent = std::clamp(layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f) * uiScale;
			const float columnWidth = iconExtent + ImGui::GetStyle().WindowPadding.x * 2.0f;
			const float margin = ImGui::GetStyle().WindowPadding.y * 2.0f;
			const float height = std::max(0.0f, viewportSize.y - horizontalHeight - margin);
			ImGui::SetNextWindowPos(
				ImVec2(viewportOrigin.x, viewportOrigin.y + horizontalHeight + margin), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(columnWidth, height), ImGuiCond_Always);
			if (!ImGui::Begin("##RendererViewportVerticalToolbarOverlay", nullptr, OverlayWindowFlags()))
			{
				ImGui::End();
				return;
			}

			DrawViewportVerticalToolbar(windowState, layer);
			ImGui::End();
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
