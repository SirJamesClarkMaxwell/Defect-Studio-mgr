#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererCommandRegistration.hpp"

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererViewportCommands.hpp"

#include <functional>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		void RegisterRendererCommand(
			CommandRegistry &registry,
			const char *id,
			const char *name,
			const char *description,
			CommandFactory factory,
			CommandFlags flags = CommandFlags::None)
		{
			auto result = registry.Register(
				CommandMeta{
					CommandID{id},
					name,
					"Renderer",
					description,
					{},
					flags},
				std::move(factory));
			if (!result)
				DS_LOG_WARN("Renderer command registration failed: {}", result.Error().technicalDetails);
		}

		Unique<ICommand> MakeAlignAxisCommand(Ref<EventBus> eventBus, int axis, CommandContext &)
		{
			return CreateRendererAlignAxisCommand(std::move(eventBus), axis);
		}

		Unique<ICommand> MakeOrbitDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::OrbitDirection direction,
			CommandContext &)
		{
			return CreateRendererOrbitDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakePanDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::OrbitDirection direction,
			CommandContext &)
		{
			return CreateRendererPanDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeOrbitQuarterTurnCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::OrbitDirection direction,
			CommandContext &)
		{
			return CreateRendererOrbitQuarterTurnCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeRollDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::RollDirection direction,
			CommandContext &)
		{
			return CreateRendererRollDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeZoomDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::ZoomDirection direction,
			CommandContext &)
		{
			return CreateRendererZoomDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeFocusSelectedAtomCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererFocusSelectedAtomCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeUndoViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererUndoViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeRedoViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererRedoViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSaveCurrentViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSaveCurrentViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeCycleSavedViewCommand(Ref<EventBus> eventBus, int direction, CommandContext &)
		{
			return CreateRendererCycleSavedViewCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeExportImageCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererExportImageCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSelectionToolToggleCommand(Ref<EventBus> eventBus, SelectionToolMode tool, CommandContext &)
		{
			return CreateRendererSelectionToolToggleCommand(std::move(eventBus), tool);
		}

		Unique<ICommand> MakeGizmoOperationCommand(Ref<EventBus> eventBus, GizmoOperation operation, CommandContext &)
		{
			return CreateRendererGizmoOperationCommand(std::move(eventBus), operation);
		}

		Unique<ICommand> MakeSelectionModeSetCommand(
			Ref<EventBus> eventBus, bool pickAtoms, bool pickBonds, bool pickLabels, CommandContext &)
		{
			return CreateRendererSelectionModeSetCommand(std::move(eventBus), pickAtoms, pickBonds, pickLabels);
		}

		Unique<ICommand> MakeAddAtomPopupCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererAddAtomPopupCommand(std::move(eventBus));
		}

		// No keybinding: invoked programmatically by RendererPanel on gizmo drag release, with the
		// drag result passed via context (see GizmoTransformPayload). Not user-invokable from the
		// command palette since it needs that payload to do anything.
		Unique<ICommand> MakeCommitGizmoTransformCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			CommandContext &context)
		{
			GizmoTransformPayload *payload = context.TryGet<GizmoTransformPayload>("gizmo.transform_payload");
			if (payload == nullptr)
				return nullptr;
			return CreateTransformSelectedAtomsCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), *payload);
		}

		Unique<ICommand> MakeLabelsToggleCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsToggleCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsToggleSelectedBondCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsToggleSelectedBondCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsToggleSelectedAngleCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsToggleSelectedAngleCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsRemoveSelectedBondCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsRemoveSelectedBondCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsShowAllBondCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsShowAllBondCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsRemoveAllBondCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsRemoveAllBondCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsRemoveSelectedAngleCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsRemoveSelectedAngleCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsShowAllAngleCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsShowAllAngleCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsRemoveAllAngleCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsRemoveAllAngleCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLabelsToggleBondAlignmentCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLabelsToggleBondAlignmentCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeUndoLabelsCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererUndoLabelsCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeRedoLabelsCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererRedoLabelsCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeHideSelectionCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererHideSelectionCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeShowAllCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererShowAllCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSelectionInvertCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSelectionInvertCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSelectAllCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSelectAllCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeToolNoneCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSelectionToolToggleCommand(std::move(eventBus), SelectionToolMode::None);
		}

		Unique<ICommand> MakeCursor3DToolToggleCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSelectionToolToggleCommand(std::move(eventBus), SelectionToolMode::Cursor3D);
		}

		Unique<ICommand> MakeSetAsDefaultViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSetAsDefaultViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeApplyDefaultViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererApplyDefaultViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeDeleteSelectedAtomsCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			CommandContext &)
		{
			return CreateDeleteSelectedAtomsCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(elementPropertiesTable));
		}

		Unique<ICommand> MakeConnectSelectedAtomsCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			CommandContext &)
		{
			return CreateConnectSelectedAtomsCommand(std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable));
		}

		Unique<ICommand> MakeDuplicateSelectedAtomsCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			CommandContext &)
		{
			return CreateDuplicateSelectedAtomsCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(elementPropertiesTable));
		}

		Unique<ICommand> MakeNudgeSelectedAtomsCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			glm::vec2 screenDirection,
			CommandContext &)
		{
			return CreateNudgeSelectedAtomsCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), screenDirection);
		}

		Unique<ICommand> MakeCopySelectedAtomsCommand(
			WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, CommandContext &)
		{
			return CreateCopySelectedAtomsCommand(std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable));
		}

		Unique<ICommand> MakePasteAtomsCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			CommandContext &)
		{
			return CreatePasteAtomsCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(elementPropertiesTable));
		}

		// Payload-driven, like MakeCommitGizmoTransformCommand - the context menu builds
		// ChangeAtomTypePayload with the typed element symbol and calls Execute() with it attached.
		// Not user-invokable from the command palette without that payload.
		Unique<ICommand> MakeChangeSelectedAtomTypeCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			CommandContext &context)
		{
			ChangeAtomTypePayload *payload = context.TryGet<ChangeAtomTypePayload>("atom_edit.change_type_payload");
			if (payload == nullptr)
				return nullptr;
			return CreateChangeSelectedAtomTypeCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(elementPropertiesTable), *payload);
		}

		// Payload-driven, like MakeChangeSelectedAtomTypeCommand - ObjectPropertiesPanel builds
		// AtomPropertiesPayload with the edited field(s) and calls Execute() with it attached.
		Unique<ICommand> MakeSetAtomPropertiesCommand(
			WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer, CommandContext &context)
		{
			AtomPropertiesPayload *payload = context.TryGet<AtomPropertiesPayload>("atom_edit.set_properties_payload");
			if (payload == nullptr)
				return nullptr;
			return CreateSetAtomPropertiesCommand(std::move(domainLayer), std::move(rendererLayer), *payload);
		}

		// Payload-driven, like MakeSetAtomPropertiesCommand - the Element Catalog panel builds
		// SetElementStylePayload (previousStyle captured before its live drag preview started) and
		// calls Execute() with it attached.
		Unique<ICommand> MakeSetElementStyleCommand(WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, CommandContext &context)
		{
			SetElementStylePayload *payload = context.TryGet<SetElementStylePayload>("atom_edit.set_element_style_payload");
			if (payload == nullptr)
				return nullptr;
			return CreateSetElementStyleCommand(std::move(rendererLayer), std::move(atomStyleTable), *payload);
		}

		// Payload-driven, like MakeSetElementStyleCommand - the Bond Settings panel builds
		// SetBondSettingsPayload (global cutoff scale + per-pair overrides, or the structure's
		// current settings unchanged for a plain "rebuild bonds now") and calls Execute() with it
		// attached.
		Unique<ICommand> MakeSetBondSettingsCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			CommandContext &context)
		{
			SetBondSettingsPayload *payload = context.TryGet<SetBondSettingsPayload>("bond_edit.set_settings_payload");
			if (payload == nullptr)
				return nullptr;
			return CreateSetBondSettingsCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(elementPropertiesTable), *payload);
		}

		Unique<ICommand> MakeAddAtomAtCoordinatesCommand(
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			CommandContext &context)
		{
			AddAtomAtCoordinatesPayload *payload = context.TryGet<AddAtomAtCoordinatesPayload>("atom_edit.add_atom_payload");
			if (payload == nullptr)
				return nullptr;
			return CreateAddAtomAtCoordinatesCommand(
				std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(elementPropertiesTable), *payload);
		}
	}

	void RegisterRendererCommands(
		CommandRegistry &registry,
		Ref<EventBus> eventBus,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable)
	{
		using namespace RendererEvents::Viewport;

		RegisterRendererCommand(
			registry,
			"renderer.align_axis_a",
			"Renderer: Align to a axis",
			"Align active renderer viewport to lattice axis a.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 0));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_b",
			"Renderer: Align to b axis",
			"Align active renderer viewport to lattice axis b.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 1));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_c",
			"Renderer: Align to c axis",
			"Align active renderer viewport to lattice axis c.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 2));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_a_star",
			"Renderer: Align to a* axis",
			"Align active renderer viewport to reciprocal lattice axis a*.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 3));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_b_star",
			"Renderer: Align to b* axis",
			"Align active renderer viewport to reciprocal lattice axis b*.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 4));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_c_star",
			"Renderer: Align to c* axis",
			"Align active renderer viewport to reciprocal lattice axis c*.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 5));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_left",
			"Renderer: Orbit left",
			"Orbit active renderer viewport left.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_right",
			"Renderer: Orbit right",
			"Orbit active renderer viewport right.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_up",
			"Renderer: Orbit up",
			"Orbit active renderer viewport up.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Up));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_down",
			"Renderer: Orbit down",
			"Orbit active renderer viewport down.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Down));
		RegisterRendererCommand(
			registry,
			"renderer.pan_left",
			"Renderer: Pan left",
			"Pan active renderer viewport left.",
			std::bind_front(MakePanDirectionCommand, eventBus, OrbitDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.pan_right",
			"Renderer: Pan right",
			"Pan active renderer viewport right.",
			std::bind_front(MakePanDirectionCommand, eventBus, OrbitDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.pan_up",
			"Renderer: Pan up",
			"Pan active renderer viewport up.",
			std::bind_front(MakePanDirectionCommand, eventBus, OrbitDirection::Up));
		RegisterRendererCommand(
			registry,
			"renderer.pan_down",
			"Renderer: Pan down",
			"Pan active renderer viewport down.",
			std::bind_front(MakePanDirectionCommand, eventBus, OrbitDirection::Down));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_left_90",
			"Renderer: Orbit left 90",
			"Orbit active renderer viewport left by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_right_90",
			"Renderer: Orbit right 90",
			"Orbit active renderer viewport right by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_up_90",
			"Renderer: Orbit up 90",
			"Orbit active renderer viewport up by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Up));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_down_90",
			"Renderer: Orbit down 90",
			"Orbit active renderer viewport down by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Down));
		RegisterRendererCommand(
			registry,
			"renderer.roll_left",
			"Renderer: Roll left",
			"Roll active renderer viewport left.",
			std::bind_front(MakeRollDirectionCommand, eventBus, RollDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.roll_right",
			"Renderer: Roll right",
			"Roll active renderer viewport right.",
			std::bind_front(MakeRollDirectionCommand, eventBus, RollDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.zoom_in",
			"Renderer: Zoom in",
			"Zoom active renderer viewport in.",
			std::bind_front(MakeZoomDirectionCommand, eventBus, ZoomDirection::In));
		RegisterRendererCommand(
			registry,
			"renderer.zoom_out",
			"Renderer: Zoom out",
			"Zoom active renderer viewport out.",
			std::bind_front(MakeZoomDirectionCommand, eventBus, ZoomDirection::Out));
		RegisterRendererCommand(
			registry,
			"renderer.focus_selected_atom",
			"Renderer: Focus selected atom",
			"Focus active renderer viewport on the selected atom.",
			std::bind_front(MakeFocusSelectedAtomCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.undo_view",
			"Renderer: Undo view",
			"Undo the last view change in the active renderer viewport.",
			std::bind_front(MakeUndoViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.redo_view",
			"Renderer: Redo view",
			"Redo the last view change in the active renderer viewport.",
			std::bind_front(MakeRedoViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.save_current",
			"Renderer: Save current view",
			"Save the active renderer viewport camera view.",
			std::bind_front(MakeSaveCurrentViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.cycle_next",
			"Renderer: Next saved view",
			"Cycle to the next saved renderer viewport camera view.",
			std::bind_front(MakeCycleSavedViewCommand, eventBus, 1));
		RegisterRendererCommand(
			registry,
			"renderer.view.cycle_previous",
			"Renderer: Previous saved view",
			"Cycle to the previous saved renderer viewport camera view.",
			std::bind_front(MakeCycleSavedViewCommand, eventBus, -1));
		RegisterRendererCommand(
			registry,
			"renderer.export.image",
			"Renderer: Export image (PNG)",
			"Save the active renderer viewport's current frame to exports/ as a PNG.",
			std::bind_front(MakeExportImageCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.box_select_toggle",
			"Renderer: Toggle box-select",
			"Toggle box drag-select on the active renderer viewport.",
			std::bind_front(MakeSelectionToolToggleCommand, eventBus, SelectionToolMode::Box));
		RegisterRendererCommand(
			registry,
			"renderer.selection.circle_select_toggle",
			"Renderer: Toggle circle-select",
			"Toggle circle drag-select on the active renderer viewport.",
			std::bind_front(MakeSelectionToolToggleCommand, eventBus, SelectionToolMode::Circle));
		RegisterRendererCommand(
			registry,
			"renderer.gizmo.mode_translate",
			"Renderer: Gizmo - Translate",
			"Switch the active renderer viewport's transform gizmo to translate (move).",
			std::bind_front(MakeGizmoOperationCommand, eventBus, GizmoOperation::Translate));
		RegisterRendererCommand(
			registry,
			"renderer.gizmo.mode_rotate",
			"Renderer: Gizmo - Rotate",
			"Switch the active renderer viewport's transform gizmo to rotate.",
			std::bind_front(MakeGizmoOperationCommand, eventBus, GizmoOperation::Rotate));
		RegisterRendererCommand(
			registry,
			"renderer.gizmo.mode_scale",
			"Renderer: Gizmo - Scale",
			"Switch the active renderer viewport's transform gizmo to scale.",
			std::bind_front(MakeGizmoOperationCommand, eventBus, GizmoOperation::Scale));
		RegisterRendererCommand(
			registry,
			"renderer.selection.mode_atoms",
			"Renderer: Selection mode - Atoms",
			"Clicking in the active renderer viewport can only select atoms.",
			std::bind_front(MakeSelectionModeSetCommand, eventBus, true, false, false));
		RegisterRendererCommand(
			registry,
			"renderer.selection.mode_atoms_bonds",
			"Renderer: Selection mode - Atoms + Bonds",
			"Clicking in the active renderer viewport can select atoms or bonds.",
			std::bind_front(MakeSelectionModeSetCommand, eventBus, true, true, false));
		RegisterRendererCommand(
			registry,
			"renderer.selection.mode_bonds_labels",
			"Renderer: Selection mode - Bonds + Labels",
			"Clicking in the active renderer viewport can select bonds or pinned labels, not atoms - "
			"lets a label be gizmo-dragged without risking an accidental atom drag.",
			std::bind_front(MakeSelectionModeSetCommand, eventBus, false, true, true));
		RegisterRendererCommand(
			registry,
			"renderer.selection.mode_all",
			"Renderer: Selection mode - Atoms + Bonds + Labels",
			"Clicking in the active renderer viewport can select atoms, bonds, or pinned labels.",
			std::bind_front(MakeSelectionModeSetCommand, eventBus, true, true, true));
		RegisterRendererCommand(
			registry,
			"renderer.atoms.open_add_popup",
			"Renderer: Add atom...",
			"Open the Add Atom popup for the active renderer viewport.",
			std::bind_front(MakeAddAtomPopupCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.toggle",
			"Renderer: Toggle all bond-length labels",
			"Toggle auto bond-length label visibility for every bond on the active renderer viewport.",
			std::bind_front(MakeLabelsToggleCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.toggle_selected_bond",
			"Renderer: Toggle selected bond-length labels",
			"Pin/unpin a bond-length label for every bonded pair within the current selection (2+ "
			"atoms) on the active renderer viewport.",
			std::bind_front(MakeLabelsToggleSelectedBondCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.toggle_selected_angle",
			"Renderer: Toggle selected angle label",
			"Pin/unpin the angle label for a 3-atom selection on the active renderer viewport.",
			std::bind_front(MakeLabelsToggleSelectedAngleCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.remove_selected_bond",
			"Renderer: Remove selected bond-length labels",
			"Remove bond-length label pins for every bonded pair within the current selection on the "
			"active renderer viewport.",
			std::bind_front(MakeLabelsRemoveSelectedBondCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.show_all_bond",
			"Renderer: Show all bond-length labels",
			"Pin a bond-length label for every bonded pair among visible atoms on the active renderer "
			"viewport.",
			std::bind_front(MakeLabelsShowAllBondCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.remove_all_bond",
			"Renderer: Remove all bond-length labels",
			"Remove bond-length label pins for every bonded pair among visible atoms on the active "
			"renderer viewport.",
			std::bind_front(MakeLabelsRemoveAllBondCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.remove_selected_angle",
			"Renderer: Remove selected angle labels",
			"Remove angle label pins within the current selection on the active renderer viewport.",
			std::bind_front(MakeLabelsRemoveSelectedAngleCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.show_all_angle",
			"Renderer: Show all angle labels",
			"Pin angle labels for every bonded triple among visible atoms on the active renderer "
			"viewport.",
			std::bind_front(MakeLabelsShowAllAngleCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.remove_all_angle",
			"Renderer: Remove all angle labels",
			"Remove angle label pins among visible atoms on the active renderer viewport.",
			std::bind_front(MakeLabelsRemoveAllAngleCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.toggle_bond_alignment",
			"Renderer: Toggle bond label alignment",
			"Toggle every bond-length label pin between reading along its bond direction (default) "
			"and staying upright, on the active renderer viewport.",
			std::bind_front(MakeLabelsToggleBondAlignmentCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.undo",
			"Renderer: Undo label change",
			"Undo the last pinned measurement label edit (add/remove/flip/gizmo move) in the active "
			"renderer viewport. Local to labels, separate from the global Ctrl+Z domain undo.",
			std::bind_front(MakeUndoLabelsCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.labels.redo",
			"Renderer: Redo label change",
			"Redo the last undone pinned measurement label edit in the active renderer viewport.",
			std::bind_front(MakeRedoLabelsCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.gizmo.commit_transform",
			"Renderer: Commit gizmo transform",
			"Apply a completed viewport gizmo drag to the domain structure (undoable).",
			std::bind_front(MakeCommitGizmoTransformCommand, domainLayer, rendererLayer, atomStyleTable),
			CommandFlags::HiddenFromPalette);
		RegisterRendererCommand(
			registry,
			"renderer.selection.hide",
			"Renderer: Hide selection",
			"Hide the currently selected atoms in the active renderer viewport.",
			std::bind_front(MakeHideSelectionCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.show_all",
			"Renderer: Show all",
			"Make all atoms visible in the active renderer viewport.",
			std::bind_front(MakeShowAllCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.invert",
			"Renderer: Invert selection",
			"Invert the atom selection in the active renderer viewport.",
			std::bind_front(MakeSelectionInvertCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.delete",
			"Renderer: Delete selected atoms",
			"Delete the currently selected atoms from the structure (undoable).",
			std::bind_front(MakeDeleteSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable, elementPropertiesTable));
		RegisterRendererCommand(
			registry,
			"renderer.bonds.connect",
			"Renderer: Connect selected atoms",
			"Add a manual bond between the 2 currently selected atoms in the active renderer viewport "
			"(undoable).",
			std::bind_front(MakeConnectSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable));
		RegisterRendererCommand(
			registry,
			"renderer.selection.duplicate",
			"Renderer: Duplicate selected atoms",
			"Duplicate the currently selected atoms next to themselves (undoable).",
			std::bind_front(MakeDuplicateSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable, elementPropertiesTable));
		RegisterRendererCommand(
			registry,
			"renderer.selection.copy",
			"Renderer: Copy selected atoms",
			"Copy the currently selected atoms to the in-process clipboard.",
			std::bind_front(MakeCopySelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable));
		RegisterRendererCommand(
			registry,
			"renderer.selection.paste",
			"Renderer: Paste atoms",
			"Insert the clipboard's atoms next to their copied position (undoable).",
			std::bind_front(MakePasteAtomsCommand, domainLayer, rendererLayer, atomStyleTable, elementPropertiesTable));
		RegisterRendererCommand(
			registry,
			"renderer.selection.change_type",
			"Renderer: Change atom type",
			"Change the currently selected atoms' element (undoable).",
			std::bind_front(MakeChangeSelectedAtomTypeCommand, domainLayer, rendererLayer, atomStyleTable, elementPropertiesTable),
			CommandFlags::HiddenFromPalette);
		RegisterRendererCommand(
			registry,
			"renderer.selection.set_atom_properties",
			"Renderer: Set atom properties",
			"Set the selected atom's label/charge/magnetization/occupancy/selective dynamics (undoable).",
			std::bind_front(MakeSetAtomPropertiesCommand, domainLayer, rendererLayer),
			CommandFlags::HiddenFromPalette);
		RegisterRendererCommand(
			registry,
			"renderer.selection.set_element_style",
			"Renderer: Set element style",
			"Set an element's color/radius in the Element Catalog (undoable).",
			std::bind_front(MakeSetElementStyleCommand, rendererLayer, atomStyleTable),
			CommandFlags::HiddenFromPalette);
		RegisterRendererCommand(
			registry,
			"renderer.bonds.set_settings",
			"Renderer: Set bond settings",
			"Change bond generation settings (global cutoff scale, per-pair overrides) and regenerate "
			"Auto bonds from them - also how a plain rebuild-bonds-now works (undoable).",
			std::bind_front(MakeSetBondSettingsCommand, domainLayer, rendererLayer, atomStyleTable, elementPropertiesTable),
			CommandFlags::HiddenFromPalette);
		RegisterRendererCommand(
			registry,
			"renderer.atoms.add_at_coordinates",
			"Renderer: Add atom at coordinates",
			"Insert a new atom at explicit coordinates in the active renderer viewport (undoable).",
			std::bind_front(MakeAddAtomAtCoordinatesCommand, domainLayer, rendererLayer, atomStyleTable, elementPropertiesTable),
			CommandFlags::HiddenFromPalette);
		RegisterRendererCommand(
			registry,
			"renderer.selection.select_all",
			"Renderer: Select all",
			"Select every atom in the active renderer viewport.",
			std::bind_front(MakeSelectAllCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.tool_none",
			"Renderer: Selection tool - None",
			"Clear the active drag-select/3D-cursor tool on the active renderer viewport.",
			std::bind_front(MakeToolNoneCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.cursor3d_tool_toggle",
			"Renderer: Toggle 3D cursor tool",
			"Toggle the click-to-place 3D cursor tool on the active renderer viewport.",
			std::bind_front(MakeCursor3DToolToggleCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.nudge_up",
			"Renderer: Nudge selection up",
			"Move the currently selected atoms one small step along the viewport camera's up axis (undoable).",
			std::bind_front(MakeNudgeSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable, glm::vec2(0.0f, 1.0f)));
		RegisterRendererCommand(
			registry,
			"renderer.selection.nudge_down",
			"Renderer: Nudge selection down",
			"Move the currently selected atoms one small step against the viewport camera's up axis (undoable).",
			std::bind_front(MakeNudgeSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable, glm::vec2(0.0f, -1.0f)));
		RegisterRendererCommand(
			registry,
			"renderer.selection.nudge_left",
			"Renderer: Nudge selection left",
			"Move the currently selected atoms one small step against the viewport camera's right axis (undoable).",
			std::bind_front(MakeNudgeSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable, glm::vec2(-1.0f, 0.0f)));
		RegisterRendererCommand(
			registry,
			"renderer.selection.nudge_right",
			"Renderer: Nudge selection right",
			"Move the currently selected atoms one small step along the viewport camera's right axis (undoable).",
			std::bind_front(MakeNudgeSelectedAtomsCommand, domainLayer, rendererLayer, atomStyleTable, glm::vec2(1.0f, 0.0f)));
		RegisterRendererCommand(
			registry,
			"renderer.view.set_as_project_default",
			"Renderer: Set as default view",
			"Save the active renderer viewport's current view as the session default view.",
			std::bind_front(MakeSetAsDefaultViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.apply_project_default",
			"Renderer: Apply default view",
			"Apply the session default view to the active renderer viewport.",
			std::bind_front(MakeApplyDefaultViewCommand, eventBus));
	}
} // namespace DefectStudio
