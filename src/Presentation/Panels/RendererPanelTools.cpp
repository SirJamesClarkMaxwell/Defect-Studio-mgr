#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"

namespace DefectStudio
{
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
