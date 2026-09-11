#include "Core/dspch.hpp"

#include "Presentation/Panels/StructureCreationTabsPanel.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <imgui.h>

#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportInput.hpp"
#include "Presentation/Panels/ViewportInteraction.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportToolbars.hpp"
#include "Renderer/CrystalStructurePreviewWindow.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr std::size_t PreviewWindowCount = 3;
		constexpr float kSplitterThickness = 5.0f;
		// A pane below this is not worth rendering and its FBO would be degenerate; it also stops a
		// splitter from being dragged into a corner it cannot be dragged out of.
		constexpr float kMinimumPaneExtent = 60.0f;

		[[nodiscard]] std::string SupercellSuffix(const glm::ivec3 &counts)
		{
			return "(" + std::to_string(counts.x) + "x" + std::to_string(counts.y) + "x" + std::to_string(counts.z) + ")";
		}

		void HashCombine(std::size_t &seed, std::size_t value)
		{
			seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
		}

		void HashStructure(std::size_t &seed, const CrystalStructure &structure)
		{
			HashCombine(seed, std::hash<std::size_t>{}(structure.atoms.size()));
			for (const AtomSite &atom : structure.atoms)
			{
				HashCombine(seed, std::hash<std::string>{}(atom.species));
				HashCombine(seed, std::hash<float>{}(atom.fractional.x));
				HashCombine(seed, std::hash<float>{}(atom.fractional.y));
				HashCombine(seed, std::hash<float>{}(atom.fractional.z));
			}
			for (const glm::vec3 &vector : structure.cell.vectors)
			{
				HashCombine(seed, std::hash<float>{}(vector.x));
				HashCombine(seed, std::hash<float>{}(vector.y));
				HashCombine(seed, std::hash<float>{}(vector.z));
			}
		}

		[[nodiscard]] float SanitizePaneExtent(float value)
		{
			return std::max(value, 1.0f);
		}

		// Draggable splitter. `fraction` is the share of `totalExtent` that belongs to the pane on
		// the FAR side (right of a vertical splitter, below a horizontal one), which is why the
		// horizontal case flips the delta's sign.
		void DrawSplitter(const char *id, bool vertical, float totalExtent, float &fraction)
		{
			const ImVec2 size = vertical
				? ImVec2(kSplitterThickness, ImGui::GetContentRegionAvail().y)
				: ImVec2(ImGui::GetContentRegionAvail().x, kSplitterThickness);
			ImGui::InvisibleButton(id, size);
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			{
				ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_SeparatorHovered));
			}
			if (!ImGui::IsItemActive() || totalExtent <= 0.0f)
				return;

			const float delta = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
			if (delta == 0.0f)
				return;

			const float minimumFraction = std::min(kMinimumPaneExtent / totalExtent, 0.45f);
			const float updated = fraction + (vertical ? delta : -delta) / totalExtent;
			fraction = std::clamp(updated, minimumFraction, 1.0f - minimumFraction);
		}
	} // namespace

	StructureCreationTabsPanel::StructureCreationTabsPanel(
		RendererLayer &rendererLayer,
		Ref<CreationSessionRegistry> sessionRegistry,
		Ref<EventBus> eventBus,
		WeakRef<ContextManager> contextManager,
		WeakRef<CommandRegistry> commandRegistry,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_SessionRegistry(std::move(sessionRegistry)),
		  m_EventBus(std::move(eventBus)),
		  m_ContextManager(std::move(contextManager)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable))
	{
	}

	Ref<IPanel> StructureCreationTabsPanel::Clone() const
	{
		return CreateRef<StructureCreationTabsPanel>(*this);
	}

	void StructureCreationTabsPanel::Render()
	{
		if (m_SessionRegistry == nullptr)
			return;

		// Copied, not iterated in place: rendering a session can publish RendererTabClosed, which
		// removes a session from the registry mid-loop.
		const CreationSessionRegistry::SessionList sessions = m_SessionRegistry->Sessions();
		for (const Ref<CreationSession> &session : sessions)
		{
			// Draft sessions render too: the panes ARE the draft's viewport while it is being
			// edited, not a reward for handing it to the Structure Hub.
			if (session != nullptr)
				renderSession(*session);
		}
	}

	RendererWindowState *StructureCreationTabsPanel::findPaneWindow(const CreationSession &session, int paneIndex)
	{
		const auto index = static_cast<std::size_t>(paneIndex);
		if (index >= session.previewWindowIds.size() || session.previewWindowIds[index].empty())
			return nullptr;
		for (RendererWindowState &window : m_RendererLayer.GetWindows())
		{
			if (window.windowId == session.previewWindowIds[index])
				return &window;
		}
		return nullptr;
	}

	void StructureCreationTabsPanel::renderSession(CreationSession &session)
	{
		const std::string sessionKey = ToString(session.sessionId);
		refreshPreviews(session);
		if (session.previewWindowIds.size() != PreviewWindowCount)
			return;

		const std::string displayName =
			session.displayName.empty() ? std::string(ToString(session.mode)) : session.displayName;
		const std::string label =
			displayName + " " + SupercellSuffix(session.supercellCounts) + "###StructureCreationSession_" + sessionKey;

		bool open = true;
		if (ImGui::Begin(label.c_str(), &open))
		{
			// Focus is reported per PANE, and only the active one counts: the keybinding context and
			// every command that resolves its target from the focused window id must land on the pane
			// the toolbars are pointing at, not on all three at once.
			const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			session.activePaneIndex = std::clamp(session.activePaneIndex, 0, static_cast<int>(PreviewWindowCount) - 1);
			// Falls back to any live pane so the toolbars keep working while the active one is hidden.
			RendererWindowState *activePane = findPaneWindow(session, session.activePaneIndex);
			for (int index = 0; activePane == nullptr && index < static_cast<int>(PreviewWindowCount); ++index)
				activePane = findPaneWindow(session, index);

			if (activePane != nullptr)
			{
				DrawViewportToolbar(*activePane, m_RendererLayer);
				ImGui::Separator();
				DrawViewportVerticalToolbar(*activePane, m_RendererLayer);
				ImGui::SameLine();
			}

			drawPaneLayout(session, ImGui::GetContentRegionAvail(), windowFocused);
		}
		ImGui::End();

		if (!open)
		{
			// The window X is one of the three paths into the close protocol; the coordinator
			// decides what happens to an attempt that is still running.
			closeSessionWindows(session);
			m_PreviewSignatures.erase(sessionKey);

			if (m_EventBus != nullptr)
			{
				DomainEvents::RendererTabClosed closedEvent;
				closedEvent.sessionId = session.sessionId;
				m_EventBus->Publish(closedEvent);
			}
		}
	}

	void StructureCreationTabsPanel::drawPaneLayout(
		CreationSession &session, const ImVec2 &available, bool windowFocused)
	{
		const bool basisVisible = session.previewVisible[0];
		const bool cellVisible = session.previewVisible[1];
		const bool supercellVisible = session.previewVisible[2];
		const bool topVisible = basisVisible || cellVisible;
		if (!topVisible && !supercellVisible)
		{
			ImGui::TextDisabled("All views are hidden - re-check one in the New Structure panel.");
			return;
		}

		ImGui::BeginChild("##creation_panes", available, false, ImGuiWindowFlags_NoScrollbar);
		const ImVec2 region = ImGui::GetContentRegionAvail();

		// A hidden pane's share goes to the visible ones rather than leaving a gap: hide the basis
		// and the unit cell spans the top; hide both and the supercell takes the whole window.
		const bool splitHorizontally = topVisible && supercellVisible;
		const float bottomHeight = splitHorizontally
			? SanitizePaneExtent(region.y * session.bottomFraction - kSplitterThickness * 0.5f)
			: region.y;
		const float topHeight = splitHorizontally
			? SanitizePaneExtent(region.y - bottomHeight - kSplitterThickness)
			: region.y;

		if (topVisible)
		{
			ImGui::BeginChild("##creation_top", ImVec2(region.x, topHeight), false, ImGuiWindowFlags_NoScrollbar);
			const bool splitVertically = basisVisible && cellVisible;
			const float leftWidth = splitVertically
				? SanitizePaneExtent(region.x * session.leftFraction - kSplitterThickness * 0.5f)
				: region.x;

			if (basisVisible)
				drawPane(session, 0, ImVec2(leftWidth, topHeight), windowFocused);
			if (splitVertically)
			{
				ImGui::SameLine(0.0f, 0.0f);
				DrawSplitter("##creation_vsplit", true, region.x, session.leftFraction);
				ImGui::SameLine(0.0f, 0.0f);
			}
			if (cellVisible)
			{
				const float cellWidth =
					splitVertically ? SanitizePaneExtent(region.x - leftWidth - kSplitterThickness) : region.x;
				drawPane(session, 1, ImVec2(cellWidth, topHeight), windowFocused);
			}
			ImGui::EndChild();
		}

		if (splitHorizontally)
			DrawSplitter("##creation_hsplit", false, region.y, session.bottomFraction);

		if (supercellVisible)
			drawPane(session, 2, ImVec2(region.x, bottomHeight), windowFocused);

		ImGui::EndChild();
	}

	void StructureCreationTabsPanel::drawPane(
		CreationSession &session, int paneIndex, const ImVec2 &size, bool windowFocused)
	{
		RendererWindowState *windowState = findPaneWindow(session, paneIndex);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		// Last frame's answer - the border colour and the focus event are drawn/published before the
		// viewport image exists, so they cannot wait for this frame's activating click below.
		const bool wasActive = session.activePaneIndex == paneIndex;
		UpdateViewportFocusState(*windowState, windowFocused && wasActive, m_RendererLayer, m_ContextManager);
		const ImVec2 paneSize(SanitizePaneExtent(size.x), SanitizePaneExtent(size.y));

		ImGui::PushStyleColor(
			ImGuiCol_Border, ImGui::GetStyleColorVec4(wasActive ? ImGuiCol_NavHighlight : ImGuiCol_Border));
		ImGui::BeginChild(
			("##creation_pane_" + std::to_string(paneIndex)).c_str(), paneSize, true, ImGuiWindowFlags_NoScrollbar);

		const ImVec2 contentRegion = ImGui::GetContentRegionAvail();
		m_RendererLayer.SetViewportSize(
			windowState->windowId,
			glm::vec2(SanitizePaneExtent(contentRegion.x), SanitizePaneExtent(contentRegion.y)));

		const unsigned int textureId = m_RendererLayer.RenderToFbo(
			windowState->windowId, windowState->structure, *windowState, m_RendererLayer.GetGlobalSettings());
		ImGui::Image(
			static_cast<ImTextureID>(static_cast<uintptr_t>(textureId)),
			ImVec2(windowState->viewportSize.x, windowState->viewportSize.y),
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));

		const ImVec2 imageOrigin = ImGui::GetItemRectMin();
		const ImVec2 imageSize(windowState->viewportSize.x, windowState->viewportSize.y);
		const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		// The activating press counts for this frame's input too. Deriving isActive from the index as
		// it stood BEFORE the click meant the press that activated a pane was swallowed, and a
		// box/circle select started with it never began its drag: the button was already down by the
		// time the pane went active, so IsMouseClicked never fired again.
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			session.activePaneIndex = paneIndex;
		const bool isActive = session.activePaneIndex == paneIndex;

		// Selection and the transform gizmo, in the order the main viewport uses them: the gizmo gets
		// first refusal on the frame's mouse, and only a click it did not claim becomes a pick. Panes
		// had neither - the redesign stopped RendererPanel from drawing them and nothing took over its
		// per-frame input half, so an atom in a pane could not be selected, let alone moved.
		// The whole shared chain, not just the atom gizmo: label transforms, the pin keyboard
		// shortcuts and the scene-arrow gizmo are viewport features too, and a pane that ran only
		// RenderTransformGizmo silently lost every one of them.
		const bool gizmoCapturing = isActive &&
			RunViewportGizmoChain(*windowState, imageOrigin, imageSize, hovered, m_RendererLayer, m_CommandRegistry);

		// Box/circle select: overlay, brush radius and drag dispatch. Runs before navigation because
		// the circle brush consumes the wheel event the camera would otherwise zoom with.
		const bool selectionToolConsumedMouse = isActive &&
			DrawAndDispatchSelectionTools(*windowState, imageOrigin, hovered, gizmoCapturing, m_RendererLayer);

		// Navigation on the active pane only: a stray hover must not orbit a view the toolbars are
		// not pointing at, and every pane keeps its own camera. Suppressed mid-gizmo-drag, or dragging
		// an axis handle would also orbit the camera under it.
		if (hovered && isActive && !gizmoCapturing && !selectionToolConsumedMouse)
		{
			ApplyViewportInputNavigation(*windowState, ImGui::GetIO().DeltaTime, m_LastMousePositions, m_RendererLayer);

			ImGuiIO &io = ImGui::GetIO();
			const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				!ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt;
			const ImVec2 mousePos = ImGui::GetMousePos();
			const float relX = mousePos.x - imageOrigin.x;
			const float relY = mousePos.y - imageOrigin.y;
			if (leftClicked && relX >= 0.0f && relY >= 0.0f && relX < imageSize.x && relY < imageSize.y)
				HandleViewportPick(*windowState, relX, relY, io.KeyCtrl, m_RendererLayer);
		}

		// Held-arrow nudge and pan are NOT gated on hover - they run for as long as the pane is the
		// focused viewport, so the glide does not stop when the mouse drifts off the pane mid-hold.
		if (isActive)
		{
			const float deltaTime = ImGui::GetIO().DeltaTime;
			ApplyContinuousKeyboardNudge(*windowState, deltaTime, m_RendererLayer, m_CommandRegistry);
			ApplyContinuousKeyboardPan(*windowState, deltaTime, m_RendererLayer);
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	std::size_t StructureCreationTabsPanel::computePreviewSignature(const CreationSession &session) const
	{
		std::size_t seed = 0;
		HashStructure(seed, session.draftStructure);
		HashStructure(seed, session.motifStructure);
		HashCombine(seed, std::hash<int>{}(session.supercellCounts.x));
		HashCombine(seed, std::hash<int>{}(session.supercellCounts.y));
		HashCombine(seed, std::hash<int>{}(session.supercellCounts.z));
		HashCombine(seed, std::hash<bool>{}(session.primitiveCellOverlay.has_value()));
		if (session.primitiveCellOverlay.has_value())
		{
			for (int column = 0; column < 3; ++column)
				for (int row = 0; row < 3; ++row)
					HashCombine(seed, std::hash<float>{}((*session.primitiveCellOverlay)[column][row]));
		}
		HashCombine(seed, std::hash<float>{}(session.draftStructure.bondSettings.globalCutoffScale));
		// Summed, not HashCombine'd in sequence: this is an unordered_map, and nothing promises two
		// equal maps iterate in the same order, which would make the signature flap on its own.
		std::size_t pairSeed = 0;
		for (const auto &[pairKey, scale] : session.draftStructure.bondSettings.perPairCutoffOverride)
			pairSeed += std::hash<std::string>{}(pairKey) ^ (std::hash<float>{}(scale) * 2654435761u);
		HashCombine(seed, pairSeed);
		// previewVisible is deliberately NOT hashed: a hidden pane keeps its renderer window, so
		// toggling one is a layout decision, not a content change.
		return seed;
	}

	void StructureCreationTabsPanel::refreshPreviews(CreationSession &session)
	{
		if (session.draftStructure.atoms.empty())
			return;

		const std::string sessionKey = ToString(session.sessionId);
		const std::size_t signature = computePreviewSignature(session);
		const auto cached = m_PreviewSignatures.find(sessionKey);
		const bool alreadyOpen = session.previewWindowIds.size() == PreviewWindowCount;
		if (alreadyOpen && cached != m_PreviewSignatures.end() && cached->second == signature)
			return;

		session.previewWindowIds.resize(PreviewWindowCount);

		// Exactly what Add to Project writes - the pane is a preview of the file, not a second
		// interpretation of the same counts.
		const CrystalStructure supercell = BuildSessionExportStructure(session);

		const auto show = [&](std::size_t index,
							  const CrystalStructure &structure,
							  const std::string &displayName,
							  bool showCellBox,
							  const std::optional<glm::mat3> &overlay,
							  const glm::ivec3 &overlayRepeat = glm::ivec3(1)) {
			session.previewWindowIds[index] = ShowCrystalStructurePreview(
				session.previewWindowIds[index], structure, displayName,
				m_RendererLayer, m_ElementPropertiesTable, m_AtomStyleTable,
				showCellBox, false, overlay, overlayRepeat, sessionKey);
		};

		// The motif gets no cell box: it is the atoms attached to ONE lattice point, and a box round
		// it would read as a unit cell it does not fill.
		show(0,
			session.motifStructure.atoms.empty() ? session.draftStructure : session.motifStructure,
			"Basis",
			false,
			std::nullopt);
		show(1, session.draftStructure, "Unit cell", true, session.primitiveCellOverlay);
		// Every unit cell in the supercell is outlined, not just the one at the origin: a lone box in
		// a corner disappears among the atoms, and the textbook picture of a lattice is the whole
		// framework. Cheap - the edges share the cell-box VBO and cost one extra draw call.
		show(2,
			supercell,
			"Supercell " + SupercellSuffix(session.supercellCounts),
			true,
			session.draftStructure.cell.ToMatrix(),
			session.supercellCounts);

		m_PreviewSignatures[sessionKey] = signature;
	}

	void StructureCreationTabsPanel::closeSessionWindows(CreationSession &session)
	{
		for (const std::string &windowId : session.previewWindowIds)
		{
			m_LastMousePositions.erase(windowId);
			CloseCrystalStructurePreview(windowId, m_RendererLayer);
		}
		session.previewWindowIds.clear();
	}
} // namespace DefectStudio
