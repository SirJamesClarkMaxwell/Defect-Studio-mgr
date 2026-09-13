#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <glm/geometric.hpp>
#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	// "Use 3D Cursor"/"Use Selection Center" below always fill a CARTESIAN position and force
	// Cartesian mode - simplest correct behavior without needing a fractional<->cartesian
	// conversion here (RendererStructureData carries the lattice matrices but not the domain
	// CrystalStructure::FractionalToCartesian helper); switch back to Fractional afterwards and
	// retype if that's genuinely what's needed.
	// Was a BeginPopupModal - modal semantics dim and block input to the ENTIRE app, including the
	// separate Periodic Table window this popup opens via "Choose..." below, making that button
	// non-functional (couldn't click any element while the modal held focus). A plain window behaves
	// like the Periodic Table window itself: both stay open and interactive side by side.
	void RendererPanel::drawAddAtomPopup()
	{
		constexpr const char *kPopupId = "Add Atom";
		if (!m_AddAtomPopupRequested)
			return;

		ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(kPopupId, &m_AddAtomPopupRequested, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		RendererWindowState *windowState = nullptr;
		for (RendererWindowState &candidate : m_Layer.GetWindows())
		{
			if (candidate.windowId == m_AddAtomPopupWindowId)
			{
				windowState = &candidate;
				break;
			}
		}
		if (windowState == nullptr)
		{
			ImGui::TextDisabled("Target window is no longer open.");
			if (ImGui::Button("Close"))
				m_AddAtomPopupRequested = false;
			ImGui::End();
			return;
		}

		// Reuses the Periodic Table window's own selection (RendererLayer::GetSelectedPeriodicElement)
		// instead of embedding a second grid here - the app already has a dedicated element picker,
		// no need for two.
		if (m_Layer.GetSelectedPeriodicElement().empty())
			m_Layer.GetSelectedPeriodicElement() = "C";
		ImGui::Text("Element: %s", m_Layer.GetSelectedPeriodicElement().c_str());
		ImGui::SameLine();
		if (ImGui::Button("Choose..."))
		{
			m_Layer.GetShowPeriodicTableWindow() = true;
			// Picking a species for a not-yet-inserted atom - confirming in the Periodic Table window
			// should just close it, not retype whatever atoms happen to already be selected in the
			// viewport (see drawPeriodicTableWindow's comment on this flag).
			m_Layer.GetPeriodicTableApplyOnConfirm() = false;
		}

		if (ImGui::RadioButton("Cartesian", !m_AddAtomPopupFractional))
			m_AddAtomPopupFractional = false;
		ImGui::SameLine();
		if (ImGui::RadioButton("Fractional", m_AddAtomPopupFractional))
			m_AddAtomPopupFractional = true;

		ImGui::InputFloat3("Position", &m_AddAtomPopupPosition.x, "%.4f");

		ImGui::BeginDisabled(!windowState->cursor3DPlaced);
		if (ImGui::Button("Use 3D Cursor"))
		{
			m_AddAtomPopupPosition = windowState->cursor3DPosition;
			m_AddAtomPopupFractional = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(windowState->selectedAtomIndices.empty());
		if (ImGui::Button("Use Selection Center"))
		{
			glm::vec3 centroid(0.0f);
			for (const std::size_t atomIndex : windowState->selectedAtomIndices)
				centroid += windowState->structure.atoms[atomIndex].cartesianPosition;
			centroid /= static_cast<float>(windowState->selectedAtomIndices.size());
			m_AddAtomPopupPosition = centroid;
			m_AddAtomPopupFractional = false;
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		const bool canInsert = !m_Layer.GetSelectedPeriodicElement().empty();
		ImGui::BeginDisabled(!canInsert);
		if (ImGui::Button("Insert"))
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				AddAtomAtCoordinatesPayload payload;
				payload.windowId = windowState->windowId;
				payload.species = m_Layer.GetSelectedPeriodicElement();
				payload.position = m_AddAtomPopupPosition;
				payload.isFractional = m_AddAtomPopupFractional;

				CommandContext context;
				context.Set<AddAtomAtCoordinatesPayload>("atom_edit.add_atom_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.atoms.add_at_coordinates"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Add atom failed: {}", result.Error().technicalDetails);
				else
					m_AddAtomPopupRequested = false;
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			m_AddAtomPopupRequested = false;

		ImGui::End();
	}

	// Blender-style Shift+A "what to add" menu - a short list at the mouse position; picking an entry
	// either opens that type's own dialog (Atom -> drawAddAtomPopup, same position/fractional seed
	// this menu was opened with) or adds it immediately (Label, same as the right-click "Add" submenu
	// in RendererPanel::renderViewportContextMenu does for a click position instead of this menu's
	// cursor3D/origin default).
	void RendererPanel::drawAddMenu()
	{
		constexpr const char *kPopupId = "##AddMenu";
		if (m_AddMenuRequested)
		{
			m_AddMenuRequested = false;
			ImGui::SetNextWindowPos(m_AddMenuScreenPos);
			ImGui::OpenPopup(kPopupId);
		}

		if (!ImGui::BeginPopup(kPopupId))
			return;

		if (ImGui::MenuItem("Atom..."))
		{
			m_AddAtomPopupRequested = true;
			m_AddAtomPopupWindowId = m_AddMenuWindowId;
			m_AddAtomPopupPosition = m_AddMenuPosition;
			m_AddAtomPopupFractional = m_AddMenuPositionFractional;
		}

		if (ImGui::MenuItem("Label"))
		{
			for (RendererWindowState &candidate : m_Layer.GetWindows())
			{
				if (candidate.windowId != m_AddMenuWindowId)
					continue;
				PushPinnedMeasurementUndoSnapshot(candidate);
				RendererWindowState::FreeLabel label;
				label.id = candidate.sceneRegistry.AllocateObjectId();
				label.worldPosition = m_AddMenuPosition;
				candidate.freeLabels.push_back(std::move(label));
				break;
			}
		}

		if (ImGui::MenuItem("Arrow"))
		{
			for (RendererWindowState &candidate : m_Layer.GetWindows())
			{
				if (candidate.windowId != m_AddMenuWindowId)
					continue;
				PushPinnedMeasurementUndoSnapshot(candidate);
				RendererWindowState::SceneArrow arrow = MakeDefaultSceneArrow(candidate, m_AddMenuPosition);
				arrow.id = candidate.sceneRegistry.AllocateObjectId();
				candidate.sceneArrows.push_back(std::move(arrow));
				const std::size_t newIndex = candidate.sceneArrows.size() - 1;
				candidate.selectedSceneArrows = {candidate.sceneArrows[newIndex].id};
				candidate.sceneArrowQuickEditActive = true;
				candidate.sceneArrowQuickEditIndex = newIndex;
				break;
			}
		}

		ImGui::EndPopup();
	}
} // namespace DefectStudio
