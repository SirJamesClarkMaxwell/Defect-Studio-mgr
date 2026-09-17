#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"

#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Domain/Electronic/HydrogenicOrbital.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	void DrawOrbitalAddMenu(
		RendererWindowState &windowState,
		const glm::vec3 &contextMenuWorldPosition,
		bool &anchorOrbitalToSelection)
	{
		if (ImGui::BeginMenu("Orbital"))
		{
			// Where it lands is decided once, above the presets, instead of being inferred from
			// whatever happened to be selected - which is what made "add on this atom" feel like
			// a side effect rather than a choice.
			const std::size_t selectedAtoms = windowState.selectedAtomIndices.size();
			const bool canAnchor = selectedAtoms == 1 || selectedAtoms == 2;
			std::string anchorLabel = "Na zaznaczonym atomie";
			if (canAnchor)
			{
				anchorLabel += " (";
				for (std::size_t i = 0; i < selectedAtoms; ++i)
				{
					const std::size_t atomIndex = windowState.selectedAtomIndices[i];
					if (atomIndex >= windowState.structure.atoms.size())
						continue;
					if (i > 0)
						anchorLabel += ", ";
					anchorLabel += windowState.structure.atoms[atomIndex].element;
					anchorLabel += " #" + std::to_string(atomIndex);
				}
				anchorLabel += ")";
			}
			bool anchorToSelection = anchorOrbitalToSelection && canAnchor;
			if (ImGui::MenuItem(anchorLabel.c_str(), nullptr, &anchorToSelection, canAnchor))
				anchorOrbitalToSelection = anchorToSelection;
			if (!canAnchor && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Zaznacz jeden atom (orbital atomowy lub hybryda) albo dwa (orbital molekularny).");
			ImGui::Separator();

			// One place that builds and files a new orbital, so the three entry points below
			// cannot drift apart on anchoring, undo or selection.
			auto addOrbital = [&](OrbitalPreset preset, int lobeIndex, bool anchor) {
				RendererWindowState::SceneOrbital orbital =
					MakeDefaultSceneOrbital(windowState, preset, contextMenuWorldPosition);
				if (!anchor)
				{
					orbital.anchorAtoms.clear();
					orbital.centerA = contextMenuWorldPosition;
					orbital.centerB = contextMenuWorldPosition + glm::vec3(1.5f, 0.0f, 0.0f);
				}
				orbital.lobeIndex = lobeIndex;
				orbital.id = windowState.sceneRegistry.AllocateObjectId();
				windowState.sceneOrbitals.push_back(std::move(orbital));
				SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
				return windowState.sceneOrbitals.back().id;
			};

			// The path the textbook picture actually describes: two atoms, one molecular
			// orbital between them, already lying along the bond because MakeOrbitalPreset
			// takes its orientation from centerB - centerA. Reaching that through
			// "Wiazace > sigma" and hoping the selection was right was not a path anyone found.
			if (selectedAtoms == 2 && ImGui::BeginMenu("Wiazanie miedzy zaznaczonymi atomami"))
			{
				for (const OrbitalPresetGroup group : {OrbitalPresetGroup::Bonding,
						 OrbitalPresetGroup::Antibonding, OrbitalPresetGroup::HybridBonding})
				{
					ImGui::SeparatorText(OrbitalPresetGroupName(group));
					for (const OrbitalPreset preset : OrbitalPresetsInGroup(group))
					{
						const int members = OrbitalPresetMemberCount(preset);
						// A pi or a delta has two degenerate orientations; offering the named
						// pair here is the difference between "it drew the wrong one" and a
						// choice the user made.
						if (members <= 1)
						{
							if (ImGui::MenuItem(OrbitalPresetName(preset)))
							{
								PushPinnedMeasurementUndoSnapshot(windowState);
								windowState.selectedSceneOrbitals = {addOrbital(preset, 0, true)};
							}
							continue;
						}
						if (!ImGui::BeginMenu(OrbitalPresetName(preset)))
							continue;
						for (int lobe = 0; lobe < members; ++lobe)
						{
							if (!ImGui::MenuItem(OrbitalPresetMemberName(preset, lobe)))
								continue;
							PushPinnedMeasurementUndoSnapshot(windowState);
							windowState.selectedSceneOrbitals = {addOrbital(preset, lobe, true)};
						}
						ImGui::EndMenu();
					}
				}
				ImGui::EndMenu();
			}

			// A hybridised centre is genuinely several wavefunctions, so drawing one means
			// adding several orbitals. Doing that by hand four times for an sp3 was the part
			// that made the feature look broken rather than merely manual.
			if (selectedAtoms == 1 && ImGui::BeginMenu("Cala hybrydyzacja na atomie"))
			{
				for (const OrbitalPreset preset : OrbitalPresetsInGroup(OrbitalPresetGroup::Hybrid))
				{
					const int members = OrbitalPresetMemberCount(preset);
					const std::string label =
						std::string(OrbitalPresetName(preset)) + " (" + std::to_string(members) + " platki)";
					if (!ImGui::MenuItem(label.c_str()))
						continue;
					PushPinnedMeasurementUndoSnapshot(windowState);
					std::vector<SceneObjectId> added;
					for (int lobe = 0; lobe < members; ++lobe)
						added.push_back(addOrbital(preset, lobe, true));
					windowState.selectedSceneOrbitals = std::move(added);
				}
				ImGui::EndMenu();
			}
			if (selectedAtoms == 2 || selectedAtoms == 1)
				ImGui::Separator();

			// Drawn from the physics' own filing (HydrogenicOrbital.hpp), not a list retyped
			// here - a preset added there appears in exactly one of these drawers without this
			// file being touched, and a test pins that down.
			for (const OrbitalPresetGroup group : AllOrbitalPresetGroups())
			{
				if (!ImGui::BeginMenu(OrbitalPresetGroupName(group)))
					continue;
				for (const OrbitalPreset preset : OrbitalPresetsInGroup(group))
				{
					const int members = OrbitalPresetMemberCount(preset);
					if (members <= 1)
					{
						if (ImGui::MenuItem(OrbitalPresetName(preset)))
						{
							PushPinnedMeasurementUndoSnapshot(windowState);
							windowState.selectedSceneOrbitals = {addOrbital(preset, 0, anchorToSelection)};
						}
						continue;
					}
					// Named members, not a number to guess at afterwards: p_z vs p_x, sp3 #2 vs
					// #3, pi's two perpendiculars. Same list the properties combo uses.
					if (!ImGui::BeginMenu(OrbitalPresetName(preset)))
						continue;
					for (int lobe = 0; lobe < members; ++lobe)
					{
						if (!ImGui::MenuItem(OrbitalPresetMemberName(preset, lobe)))
							continue;
						PushPinnedMeasurementUndoSnapshot(windowState);
						windowState.selectedSceneOrbitals = {addOrbital(preset, lobe, anchorToSelection)};
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
	}
} // namespace DefectStudio
