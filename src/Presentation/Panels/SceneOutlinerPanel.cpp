#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <map>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool AnyContentHidden(const RendererWindowState &windowState)
		{
			for (const RendererAtomData &atom : windowState.structure.atoms)
				if (!atom.visible)
					return true;
			for (const RendererBondData &bond : windowState.structure.bonds)
				if (!bond.visible)
					return true;
			return false;
		}

		[[nodiscard]] bool AnyAtomHidden(const RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices)
		{
			for (const std::size_t index : atomIndices)
				if (index < windowState.structure.atoms.size() && !windowState.structure.atoms[index].visible)
					return true;
			return false;
		}

		// Same mechanism as H (hide selected)/Alt+H (show all) - see ViewModifier.cpp - just applied
		// to an explicit set of atom entities instead of only the current selection.
		void SetAtomsVisible(RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices, bool visible)
		{
			SceneRegistry &scene = windowState.sceneRegistry;
			for (const std::size_t index : atomIndices)
			{
				Entity atomEntity = scene.AtomEntityAt(index);
				if (atomEntity)
					atomEntity.GetComponent<VisibilityComponent>().visible = visible;
			}
			SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);
		}

		void SetWindowContentVisible(RendererWindowState &windowState, bool visible)
		{
			entt::registry &registry = windowState.sceneRegistry.Registry();
			for (const entt::entity entity : registry.view<VisibilityComponent>())
				registry.get<VisibilityComponent>(entity).visible = visible;
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}
	} // namespace

	SceneOutlinerPanel::SceneOutlinerPanel(RendererLayer &layer, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Layer(layer)
	{
	}

	Ref<IPanel> SceneOutlinerPanel::Clone() const
	{
		return CreateRef<SceneOutlinerPanel>(*this);
	}

	void SceneOutlinerPanel::drawAtomRow(RendererWindowState &windowState, std::size_t atomIndex)
	{
		Ref<EventBus> eventBus = m_Layer.GetEventBus();

		ImGui::PushID(static_cast<int>(atomIndex));
		bool visible = windowState.structure.atoms[atomIndex].visible;
		if (ImGui::Checkbox("##atomVisible", &visible))
			SetAtomsVisible(windowState, {atomIndex}, visible);
		ImGui::SameLine();

		const bool isSelected = std::find(
									 windowState.selectedAtomIndices.begin(), windowState.selectedAtomIndices.end(), atomIndex) !=
			windowState.selectedAtomIndices.end();
		char label[64];
		std::snprintf(
			label, sizeof(label), "#%zu  (%.2f, %.2f, %.2f)", atomIndex,
			windowState.structure.atoms[atomIndex].cartesianPosition.x,
			windowState.structure.atoms[atomIndex].cartesianPosition.y,
			windowState.structure.atoms[atomIndex].cartesianPosition.z);
		ImGui::Selectable(label, isSelected);
		if (ImGui::IsItemClicked() && eventBus != nullptr)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.atomIndex = atomIndex;
			event.additive = ImGui::GetIO().KeyCtrl;
			eventBus->Publish(event);
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawSpeciesGroup(
		RendererWindowState &windowState, const std::string &species, const std::vector<std::size_t> &atomIndices)
	{
		ImGui::PushID(species.c_str());

		bool visible = !AnyAtomHidden(windowState, atomIndices);
		if (ImGui::Checkbox("##speciesVisible", &visible))
			SetAtomsVisible(windowState, atomIndices, visible);
		ImGui::SameLine();

		char groupLabel[48];
		std::snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)", species.c_str(), atomIndices.size());
		const bool open = ImGui::TreeNodeEx(
			"##species", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			// Only the expanded group's own atoms are ever iterated per frame - a species with
			// thousands of atoms (a large defect supercell's majority element) still costs one clipped
			// pass, not thousands of widgets, unless it's the group actually open on screen.
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(atomIndices.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					drawAtomRow(windowState, atomIndices[static_cast<std::size_t>(row)]);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		std::vector<RendererWindowState> &windows = m_Layer.GetWindows();
		if (m_EditingWindowIndex >= static_cast<int>(windows.size()))
			m_EditingWindowIndex = -1;
		if (m_ActiveWindowIndex >= static_cast<int>(windows.size()))
			m_ActiveWindowIndex = -1;

		if (windows.empty())
			ImGui::TextDisabled("No open structures.");

		for (int i = 0; i < static_cast<int>(windows.size()); ++i)
		{
			RendererWindowState &windowState = windows[static_cast<std::size_t>(i)];
			ImGui::PushID(i);

			bool visible = !AnyContentHidden(windowState);
			if (ImGui::Checkbox("##visible", &visible))
				SetWindowContentVisible(windowState, visible);
			ImGui::SameLine();

			if (m_EditingWindowIndex == i)
			{
				if (m_JustStartedEditing)
				{
					ImGui::SetKeyboardFocusHere();
					m_JustStartedEditing = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				const bool committed = ImGui::InputText(
					"##rename", m_EditingBuffer.data(), m_EditingBuffer.size(),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (committed || ImGui::IsItemDeactivated())
				{
					if (m_EditingBuffer[0] != '\0')
						windowState.title = m_EditingBuffer.data();
					m_EditingWindowIndex = -1;
				}
			}
			else
			{
				const bool open = ImGui::TreeNodeEx(
					"##win", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
					windowState.title.c_str());
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					m_ActiveWindowIndex = i;
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_ActiveWindowIndex = i;
					m_EditingWindowIndex = i;
					std::snprintf(m_EditingBuffer.data(), m_EditingBuffer.size(), "%s", windowState.title.c_str());
					m_JustStartedEditing = true;
				}
				if (m_ActiveWindowIndex == i && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
					ImGui::IsKeyPressed(ImGuiKey_F2, false))
				{
					m_EditingWindowIndex = i;
					std::snprintf(m_EditingBuffer.data(), m_EditingBuffer.size(), "%s", windowState.title.c_str());
					m_JustStartedEditing = true;
				}

				if (open)
				{
					// Grouped by species rather than one flat atom-per-row list - a defect supercell's
					// atom count is unwieldy to scroll flat, but its handful of distinct elements
					// aren't. Recomputed every frame (cheap relative to everything else Render() already
					// walks per window) rather than cached, since atoms can be added/removed/retyped by
					// any command between frames with no dedicated invalidation hook to hang a cache off.
					std::map<std::string, std::vector<std::size_t>> speciesGroups;
					for (std::size_t atomIndex = 0; atomIndex < windowState.structure.atoms.size(); ++atomIndex)
						speciesGroups[windowState.structure.atoms[atomIndex].element].push_back(atomIndex);

					for (const auto &[species, atomIndices] : speciesGroups)
						drawSpeciesGroup(windowState, species, atomIndices);

					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
