#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanel.hpp"

#include <cstdio>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	// Shared by every label kind (free labels, pinned bond/angle labels) - one editor for
	// RendererWindowState::LabelStyle instead of a separate control block per label kind. Outline/
	// background rows read "0 = off" like the shader they feed (labels.frag/label_background.frag).
	static void drawLabelStyleEditor(RendererWindowState::LabelStyle &style)
	{
		ImGui::TextUnformatted("Text");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleTextColor", &style.textColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleTextAlpha", &style.textAlpha, 0.0f, 1.0f, "%.2f");

		// Checkbox is a thin view over backgroundAlpha/outlineWidth themselves (0 = off, same meaning
		// the shader already gives that value) rather than a separate enabled flag - one source of
		// truth. Toggling on restores a sensible default rather than 0, since the slider/drag below
		// would otherwise show "on" at a still-invisible value.
		bool backgroundEnabled = style.backgroundAlpha > 0.0f;
		if (ImGui::Checkbox("##StyleBackgroundEnabled", &backgroundEnabled))
			style.backgroundAlpha = backgroundEnabled ? 0.85f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Background");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::BeginDisabled(!backgroundEnabled);
		ImGui::ColorEdit3("##StyleBackgroundColor", &style.backgroundColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleBackgroundAlpha", &style.backgroundAlpha, 0.01f, 1.0f, "%.2f");
		ImGui::EndDisabled();

		bool borderEnabled = style.outlineWidth > 0.0f;
		if (ImGui::Checkbox("##StyleBorderEnabled", &borderEnabled))
			style.outlineWidth = borderEnabled ? 0.02f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Border");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleOutlineColor", &style.outlineColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!borderEnabled);
		ImGui::DragFloat("Width##StyleOutlineWidth", &style.outlineWidth, 0.002f, 0.001f, 0.2f, "%.3f");
		ImGui::EndDisabled();

		// Glyph stroke (labels.frag), independent of the Border row above which only frames the
		// background quad. Screen pixels, not world units and not normalized SDF units - stays a
		// constant on-screen thickness regardless of zoom, unlike Border's 0.001-0.2 world-unit range.
		bool strokeEnabled = style.strokeWidth > 0.0f;
		if (ImGui::Checkbox("##StyleStrokeEnabled", &strokeEnabled))
			style.strokeWidth = strokeEnabled ? 2.0f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Stroke");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleStrokeColor", &style.strokeColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!strokeEnabled);
		ImGui::DragFloat("Width (px)##StyleStrokeWidth", &style.strokeWidth, 0.05f, 0.1f, 8.0f, "%.2f");
		ImGui::EndDisabled();

		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Corner radius##StyleCornerRadius", &style.cornerRadius, 0.005f, 0.0f, 0.3f, "%.3f");

		ImGui::SetNextItemWidth(140.0f);
		ImGui::DragFloat2("Padding X/Y##StylePadding", &style.padding.x, 0.005f, 0.0f, 1.0f, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Scale##StyleScale", &style.scale, 0.02f, 0.1f, 8.0f, "%.2f");
	}

	ObjectPropertiesPanel::ObjectPropertiesPanel(
		RendererLayer &layer,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<DomainLayer> domainLayer,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_DomainLayer(std::move(domainLayer))
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

		// GetLastFocusedViewportWindowId, not GetFocusedViewportWindowId - the latter clears the
		// instant ImGui focus leaves the viewport (it's meant for camera-input gating), which is
		// exactly what happens the moment this panel's own fields are clicked to edit them. Using it
		// here made every field un-editable: clicking into any InputFloat/InputText immediately
		// dropped the "no viewport focused" message before the click could even register on the
		// widget. Same fix ElectronicStructureSession.cpp already applies for the same reason.
		const std::string &focusedWindowId = m_Layer.GetLastFocusedViewportWindowId();
		RendererWindowState *windowState = nullptr;
		if (!focusedWindowId.empty())
		{
			for (RendererWindowState &candidate : m_Layer.GetWindows())
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
		else if (windowState->selectedAtomIndices.size() != 1)
		{
			ImGui::TextDisabled("Select exactly 1 atom to edit its properties.");
		}
		else
		{
			const std::size_t atomIndex = windowState->selectedAtomIndices.front();
			if (atomIndex >= windowState->structure.atoms.size())
			{
				ImGui::TextDisabled("Selected atom is no longer valid.");
			}
			else
			{
				RendererAtomData &atom = windowState->structure.atoms[atomIndex];
				Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();

				// Domain-only fields (label/charge/magnetization/occupancy/selective dynamics) have no
				// renderer-side representation at all - resolved straight from the live domain
				// structure rather than RendererAtomData, same lookup every other atom-edit command
				// uses to find it. Position/element stay renderer-first since they're already mirrored
				// there and their commit commands (gizmo transform / change type) expect that.
				Ref<StructureRecord> domainRecord;
				if (domainLayer != nullptr)
				{
					Result<AtomEditTarget> target = ResolveAtomEditTarget(m_Layer, *domainLayer, windowState->windowId);
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
					m_Layer.GetSelectedPeriodicElement() = atom.element;
					m_Layer.GetShowPeriodicTableWindow() = true;
					m_Layer.GetPeriodicTableApplyOnConfirm() = true;
				}

				ImGui::Separator();
				ImGui::Text("Position");

				glm::vec3 cartesian = atom.cartesianPosition;
				bool cartesianCommitted = false;
				ImGui::PushItemWidth(110.0f);
				ImGui::BeginGroup();
				ImGui::Text("Cartesian (A)");
				ImGui::InputFloat("X##cart", &cartesian.x, 0.0f, 0.0f, "%.4f");
				cartesianCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputFloat("Y##cart", &cartesian.y, 0.0f, 0.0f, "%.4f");
				cartesianCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputFloat("Z##cart", &cartesian.z, 0.0f, 0.0f, "%.4f");
				cartesianCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::EndGroup();

				bool fractionalCommitted = false;
				glm::vec3 fractional(0.0f);
				if (domainRecord != nullptr)
				{
					fractional = domainRecord->structure.CartesianToFractional(atom.cartesianPosition);
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::Text("Fractional");
					ImGui::InputFloat("X##frac", &fractional.x, 0.0f, 0.0f, "%.4f");
					fractionalCommitted |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::InputFloat("Y##frac", &fractional.y, 0.0f, 0.0f, "%.4f");
					fractionalCommitted |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::InputFloat("Z##frac", &fractional.z, 0.0f, 0.0f, "%.4f");
					fractionalCommitted |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::EndGroup();
				}
				ImGui::PopItemWidth();

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

		// Independent of the atom-selection branch above (0/1/many atoms selected doesn't matter
		// here) - free labels are scene-wide annotations, not a per-atom property. Reposition via
		// typed X/Y/Z here, or click-drag in the viewport (RendererPanel::handleFreeLabelInteraction).
		if (windowState != nullptr)
		{
			ImGui::Separator();
			ImGui::Text("Free labels");
			if (ImGui::Button("+ Add label"))
			{
				PushPinnedMeasurementUndoSnapshot(*windowState);
				RendererWindowState::FreeLabel label;
				label.worldPosition = windowState->cursor3DPlaced ? windowState->cursor3DPosition : glm::vec3(0.0f);
				windowState->freeLabels.push_back(std::move(label));
			}
			int labelToRemove = -1;
			for (int labelIndex = 0; labelIndex < static_cast<int>(windowState->freeLabels.size()); ++labelIndex)
			{
				RendererWindowState::FreeLabel &label = windowState->freeLabels[labelIndex];
				ImGui::PushID(labelIndex);

				char textBuffer[128];
				std::snprintf(textBuffer, sizeof(textBuffer), "%s", label.text.c_str());
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputText("##LabelText", textBuffer, sizeof(textBuffer)))
					label.text = textBuffer;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(200.0f);
				ImGui::InputFloat3("##LabelPos", &label.worldPosition.x, "%.3f");
				ImGui::SameLine();
				if (ImGui::Button("X##RemoveLabel"))
					labelToRemove = labelIndex;

				if (ImGui::TreeNode("Style##FreeLabelStyle"))
				{
					drawLabelStyleEditor(label.style);
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
			if (labelToRemove >= 0)
			{
				PushPinnedMeasurementUndoSnapshot(*windowState);
				windowState->freeLabels.erase(windowState->freeLabels.begin() + labelToRemove);
			}

			// Pinned bond/angle labels and free labels are the same kind of object for style purposes
			// (RendererWindowState::LabelStyle) even though they live in separate vectors - selecting
			// several of either/both here edits their style together, same "click, or box/circle-
			// select, then edit" flow RendererPanel::handlePinnedMeasurementInteraction/
			// handleFreeLabelInteraction/applyLabelRegionSelection build the selection with.
			ImGui::Separator();
			ImGui::Text("Selected labels");
			const std::size_t pinCount = windowState->selectedPinnedMeasurements.size();
			const std::size_t freeCount = windowState->selectedFreeLabels.size();
			if (pinCount == 0 && freeCount == 0)
			{
				ImGui::TextDisabled("Click, or box/circle-select, a label in the viewport to select it.");
			}
			else
			{
				// Per-pin controls only make sense for exactly one selected pin and nothing else.
				if (pinCount == 1 && freeCount == 0)
				{
					RendererWindowState::PinnedMeasurement &pin =
						windowState->pinnedMeasurements[windowState->selectedPinnedMeasurements[0]];
					ImGui::TextUnformatted(pin.atomIndices.size() == 2 ? "Bond length" : "Angle");
					if (pin.atomIndices.size() == 2)
					{
						ImGui::SameLine();
						ImGui::Checkbox("Align to bond##PinAlign", &pin.alignToBondDirection);
					}
				}
				else
				{
					ImGui::Text("%zu label(s) selected - style below applies to all of them", pinCount + freeCount);
				}

				// Bulk style edit: edit one representative item's style, then broadcast that same
				// value to every other selected item every frame - simplest correct way to edit N
				// independent LabelStyle structs together in immediate-mode UI without per-widget
				// delta tracking. Snaps the whole selection to the representative's starting style
				// immediately (before any edit), same "pick one, it becomes the shared value" behavior
				// as most bulk-editors, rather than showing a "mixed" state.
				const bool usedPinAsRepresentative = pinCount > 0;
				RendererWindowState::LabelStyle &representative = usedPinAsRepresentative
					? windowState->pinnedMeasurements[windowState->selectedPinnedMeasurements[0]].style
					: windowState->freeLabels[windowState->selectedFreeLabels[0]].style;
				drawLabelStyleEditor(representative);
				for (std::size_t i = usedPinAsRepresentative ? 1 : 0; i < pinCount; ++i)
					windowState->pinnedMeasurements[windowState->selectedPinnedMeasurements[i]].style = representative;
				for (std::size_t i = usedPinAsRepresentative ? 0 : 1; i < freeCount; ++i)
					windowState->freeLabels[windowState->selectedFreeLabels[i]].style = representative;
			}

			ImGui::Separator();
			ImGui::Text("Arrows");
			if (ImGui::Button("+ Add arrow"))
			{
				RendererWindowState::SceneArrow arrow;
				if (windowState->cursor3DPlaced)
				{
					arrow.start = glm::vec3(0.0f);
					arrow.end = windowState->cursor3DPosition;
				}
				windowState->sceneArrows.push_back(arrow);
			}
			int arrowToRemove = -1;
			for (int arrowIndex = 0; arrowIndex < static_cast<int>(windowState->sceneArrows.size()); ++arrowIndex)
			{
				RendererWindowState::SceneArrow &arrow = windowState->sceneArrows[arrowIndex];
				ImGui::PushID(arrowIndex);

				ImGui::TextUnformatted("Start");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(200.0f);
				ImGui::InputFloat3("##ArrowStart", &arrow.start.x, "%.3f");
				ImGui::SameLine();
				ImGui::ColorEdit3("##ArrowColor", &arrow.color.x, ImGuiColorEditFlags_NoInputs);
				ImGui::SameLine();
				if (ImGui::Button("X##RemoveArrow"))
					arrowToRemove = arrowIndex;

				ImGui::TextUnformatted("End  ");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(200.0f);
				ImGui::InputFloat3("##ArrowEnd", &arrow.end.x, "%.3f");

				ImGui::PopID();
			}
			if (arrowToRemove >= 0)
				windowState->sceneArrows.erase(windowState->sceneArrows.begin() + arrowToRemove);
		}

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
