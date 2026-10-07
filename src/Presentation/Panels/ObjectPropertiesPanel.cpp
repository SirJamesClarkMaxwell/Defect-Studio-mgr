#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanel.hpp"

#include "Presentation/Panels/ObjectPropertiesPanelSections.hpp"
#include "Presentation/Panels/ObjectPropertiesSelection.hpp"
#include "Presentation/Panels/SceneDensityEditor.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Presentation/Panels/ScenePathEditorWidget.hpp"
#include "Presentation/Panels/ViewportGizmo.hpp"

#include <array>
#include <cstdio>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	ObjectPropertiesPanel::ObjectPropertiesPanel(
		RendererLayer &layer,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<DomainLayer> domainLayer,
		std::optional<AtomStyleTable> atomStyleTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_DomainLayer(std::move(domainLayer)),
		  m_AtomStyleTable(std::move(atomStyleTable))
	{
	}

	Ref<IPanel> ObjectPropertiesPanel::Clone() const
	{
		return CreateRef<ObjectPropertiesPanel>(*this);
	}

	void ObjectPropertiesPanel::Render()
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

		DrawObjectPropertiesContent(
			m_Layer, m_CommandRegistry, m_DomainLayer, m_AtomStyleTable ? &*m_AtomStyleTable : nullptr);

		ImGui::End();
		SetVisible(windowOpen);
	}

	// Everything between the panel's Begin/End, so the viewport's N panel draws the same widgets
	// rather than a second copy of them.
	void DrawObjectPropertiesContent(
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef,
		const WeakRef<DomainLayer> &domainLayerRef, AtomStyleTable *styles)
	{
		// GetLastFocusedViewportWindowId, not GetFocusedViewportWindowId - the latter clears the
		// instant ImGui focus leaves the viewport (it's meant for camera-input gating), which is
		// exactly what happens the moment this panel's own fields are clicked to edit them. Using it
		// here made every field un-editable: clicking into any InputFloat/InputText immediately
		// dropped the "no viewport focused" message before the click could even register on the
		// widget. Same fix ElectronicStructureSession.cpp already applies for the same reason.
		const std::string &focusedWindowId = layer.GetLastFocusedViewportWindowId();
		RendererWindowState *windowState = nullptr;
		if (!focusedWindowId.empty())
		{
			for (RendererWindowState &candidate : layer.GetWindows())
			{
				if (candidate.windowId == focusedWindowId)
				{
					windowState = &candidate;
					break;
				}
			}
		}

		if (windowState == nullptr)
		{
			ImGui::TextDisabled("No renderer viewport focused.");
		}
		else
		{
			const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(*windowState);
			if (sections.Empty())
				ImGui::TextDisabled("Nic nie jest zaznaczone.");

			if (sections.atoms && windowState->selectedAtomIndices.size() != 1)
			{
				ImGui::TextDisabled("Select exactly 1 atom to edit its properties.");
			}
			else if (sections.atoms)
			{
				const std::size_t atomIndex = windowState->selectedAtomIndices.front();
				if (atomIndex >= windowState->structure.atoms.size())
				{
					ImGui::TextDisabled("Selected atom is no longer valid.");
				}
				else
				{
				RendererAtomData &atom = windowState->structure.atoms[atomIndex];
				Ref<CommandRegistry> commandRegistry = commandRegistryRef.lock();
				Ref<DomainLayer> domainLayer = domainLayerRef.lock();

				// Domain-only fields (label/charge/magnetization/occupancy/selective dynamics) have no
				// renderer-side representation at all - resolved straight from the live domain
				// structure rather than RendererAtomData, same lookup every other atom-edit command
				// uses to find it. Position/element stay renderer-first since they're already mirrored
				// there and their commit commands (gizmo transform / change type) expect that.
				Ref<StructureRecord> domainRecord;
				if (domainLayer != nullptr)
				{
					Result<AtomEditTarget> target = ResolveAtomEditTarget(layer, *domainLayer, windowState->windowId);
					if (target)
						domainRecord = target->record;
				}
				const AtomSite *domainAtom = (domainRecord != nullptr && atomIndex < domainRecord->structure.atoms.size())
					? &domainRecord->structure.atoms[atomIndex]
					: nullptr;

				ImGui::Text("Atom #%zu", atomIndex);
				ImGui::Separator();
				ImGui::PushItemWidth(140.0f);

				// Element - reuses "renderer.selection.change_type", the same command the viewport
				// context menu drives, so this stays a single undo step regardless of entry point.
				ImGui::TextUnformatted("Element");
				ImGui::SameLine();
				char speciesBuffer[16];
				std::snprintf(speciesBuffer, sizeof(speciesBuffer), "%s", atom.element.c_str());
				ImGui::InputText("##ElementText", speciesBuffer, sizeof(speciesBuffer));
				if (ImGui::IsItemDeactivatedAfterEdit() && commandRegistry != nullptr && speciesBuffer[0] != '\0')
				{
					ChangeAtomTypePayload payload;
					payload.windowId = windowState->windowId;
					payload.species = speciesBuffer;
					CommandContext context;
					context.Set<ChangeAtomTypePayload>("atom_edit.change_type_payload", std::move(payload));
					Result<CommandOutcome> result =
						commandRegistry->Execute(CommandID{"renderer.selection.change_type"}, std::move(context));
					if (!result)
						DS_LOG_WARN("Set atom element failed: {}", result.Error().technicalDetails);
				}
				ImGui::SameLine();
				if (ImGui::Button("Choose..."))
				{
					// Seeds the shared Periodic Table window with this atom's current element and asks
					// it to apply the pick back to the selection (rather than just close) once
					// confirmed - see drawPeriodicTableWindow's GetPeriodicTableApplyOnConfirm comment.
					layer.GetSelectedPeriodicElement() = atom.element;
					layer.GetShowPeriodicTableWindow() = true;
					layer.GetPeriodicTableApplyOnConfirm() = true;
				}

				ImGui::Separator();
				ImGui::Text("Position");

				glm::vec3 cartesian = atom.cartesianPosition;
				bool cartesianCommitted = false;
				bool fractionalCommitted = false;
				glm::vec3 fractional(0.0f);
				if (domainRecord != nullptr)
					fractional = domainRecord->structure.CartesianToFractional(atom.cartesianPosition);

				const char *coordinateLabels[] = {"Cartesian (A)", "Fractional"};
				const ScenePathEditorLayout coordinateLayout = MeasureScenePathEditorLayout(
					ImGui::GetContentRegionAvail().x, coordinateLabels, domainRecord != nullptr ? 2 : 1);
				const char *axisLabels[] = {"X", "Y", "Z"};
				if (ImGui::BeginTable("##AtomCoordinates", 4,
					ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
				{
					ImGui::TableSetupColumn(
						"##AtomCoordinateLabel", ImGuiTableColumnFlags_WidthFixed, coordinateLayout.labelColumnWidth);
					for (const char *axisLabel : axisLabels)
						ImGui::TableSetupColumn(
							axisLabel, ImGuiTableColumnFlags_WidthStretch, coordinateLayout.componentColumnWidth);

					const auto drawCoordinateRow = [&](const char *label, glm::vec3 &value, const char *id,
						bool &committed) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						DrawScenePathEditorLabel(label, false);
						ImGui::PushID(id);
						for (int axis = 0; axis < 3; ++axis)
						{
							ImGui::TableSetColumnIndex(axis + 1);
							ImGui::PushStyleColor(
								ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ViewportTransformAxisColor(axis)));
							ImGui::TextUnformatted(axisLabels[axis]);
							ImGui::PopStyleColor();
							ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
							ImGui::SetNextItemWidth(-1.0f);
							ImGui::PushID(axis);
							ImGui::InputFloat("##value", &value[axis], 0.0f, 0.0f, "%.4f");
							committed |= ImGui::IsItemDeactivatedAfterEdit();
							ImGui::PopID();
						}
						ImGui::PopID();
					};

					drawCoordinateRow("Cartesian (A)", cartesian, "cart", cartesianCommitted);
					if (domainRecord != nullptr)
						drawCoordinateRow("Fractional", fractional, "frac", fractionalCommitted);
					ImGui::EndTable();
				}

				if ((cartesianCommitted || fractionalCommitted) && commandRegistry != nullptr)
				{
					const glm::vec3 newPosition = fractionalCommitted && domainRecord != nullptr
						? domainRecord->structure.FractionalToCartesian(fractional)
						: cartesian;

					GizmoTransformPayload payload;
					payload.windowId = windowState->windowId;
					payload.atomIndices = {atomIndex};
					payload.afterPositions = {newPosition};
					payload.description = "Set atom position";

					CommandContext context;
					context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
					Result<CommandOutcome> result =
						commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
					if (!result)
						DS_LOG_WARN("Set atom position failed: {}", result.Error().technicalDetails);
				}

				if (domainAtom != nullptr)
				{
					ImGui::Separator();
					ImGui::Text("Other properties");

					// Every commit below sends the *whole* current set of these fields, not a partial
					// patch - AtomPropertiesPayload has no "which fields changed" flag, so any field
					// left at its struct default would silently blast away the others' live values.
					auto commitProperties = [&](const AtomSite &edited)
					{
						if (commandRegistry == nullptr)
							return;
						AtomPropertiesPayload payload;
						payload.windowId = windowState->windowId;
						payload.atomIndex = atomIndex;
						payload.label = edited.label;
						payload.charge = edited.charge;
						payload.magnetization = edited.magnetization;
						payload.occupancy = edited.occupancy;
						payload.hasSelectiveDynamics = edited.hasSelectiveDynamics;
						payload.selectiveDynamics = edited.selectiveDynamics;

						CommandContext context;
						context.Set<AtomPropertiesPayload>("atom_edit.set_properties_payload", std::move(payload));
						Result<CommandOutcome> result = commandRegistry->Execute(
							CommandID{"renderer.selection.set_atom_properties"}, std::move(context));
						if (!result)
							DS_LOG_WARN("Set atom properties failed: {}", result.Error().technicalDetails);
					};

					AtomSite edited = *domainAtom;

					char labelBuffer[64];
					std::snprintf(labelBuffer, sizeof(labelBuffer), "%s", domainAtom->label.c_str());
					ImGui::InputText("Label", labelBuffer, sizeof(labelBuffer));
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.label = labelBuffer;
						commitProperties(edited);
					}

					float occupancy = domainAtom->occupancy;
					ImGui::DragFloat("Occupancy", &occupancy, 0.01f, 0.0f, 1.0f, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.occupancy = occupancy;
						commitProperties(edited);
					}

					float charge = domainAtom->charge;
					ImGui::InputFloat("Charge", &charge, 0.0f, 0.0f, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.charge = charge;
						commitProperties(edited);
					}

					float magnetization = domainAtom->magnetization;
					ImGui::InputFloat("Magnetization", &magnetization, 0.0f, 0.0f, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.magnetization = magnetization;
						commitProperties(edited);
					}

					bool hasSelectiveDynamics = domainAtom->hasSelectiveDynamics;
					if (ImGui::Checkbox("Selective dynamics", &hasSelectiveDynamics))
					{
						edited.hasSelectiveDynamics = hasSelectiveDynamics;
						commitProperties(edited);
					}
					if (domainAtom->hasSelectiveDynamics)
					{
						std::array<bool, 3> selectiveDynamics = domainAtom->selectiveDynamics;
						bool selectiveDynamicsChanged = false;
						selectiveDynamicsChanged |= ImGui::Checkbox("X##selDyn", &selectiveDynamics[0]);
						ImGui::SameLine();
						selectiveDynamicsChanged |= ImGui::Checkbox("Y##selDyn", &selectiveDynamics[1]);
						ImGui::SameLine();
						selectiveDynamicsChanged |= ImGui::Checkbox("Z##selDyn", &selectiveDynamics[2]);
						if (selectiveDynamicsChanged)
						{
							edited.selectiveDynamics = selectiveDynamics;
							commitProperties(edited);
						}
					}
				}

				ImGui::PopItemWidth();
				}
			}
		}

		// Independent of the atom-selection branch above (0/1/many atoms selected doesn't matter
		// here) - free labels are scene-wide annotations, not a per-atom property. Reposition via
		// typed X/Y/Z here, or click-drag in the viewport (RendererPanel::handleFreeLabelInteraction).
		if (windowState != nullptr)
		{
			const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(*windowState);
			if (sections.labels)
				DrawSelectedLabelProperties(*windowState);
			if (sections.orbitals)
				DrawSelectedSceneOrbitalSection(*windowState);
			if (sections.planes)
				DrawSelectedScenePlaneSection(*windowState);
			if (sections.densities)
				DrawSelectedSceneDensitySection(*windowState);
			if (sections.paths)
				DrawSelectedScenePathSection(*windowState);
			if (sections.defectFrame)
				DrawSelectedDefectFrameSection(*windowState, commandRegistryRef.lock().get());
			if (sections.vacancies)
				DrawSelectedVacancySection(*windowState, layer, domainLayerRef.lock().get(),
					commandRegistryRef.lock().get(), styles);

			if (!ImGui::CollapsingHeader("Wszystkie obiekty"))
				return;

			DrawAllLabelRows(*windowState);
			DrawAllSceneOrbitalRows(*windowState);
			DrawAllScenePlaneRows(*windowState);
		}
	}
} // namespace DefectStudio
