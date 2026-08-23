#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/RendererConfig.hpp"
#include "Renderer/RendererTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DefectStudio::RendererEvents::Config
{
	struct Applied final : public BusEvent
	{
		Applied() = default;
		Applied(RendererConfig rendererConfig, bool persisted)
			: config(std::move(rendererConfig)), wasPersisted(persisted)
		{
		}

		RendererConfig config;
		bool wasPersisted = false;
	};
} // namespace DefectStudio::RendererEvents::Config

namespace DefectStudio::RendererEvents::Viewport
{
	// Emitowane przez RendererPanel gdy viewport jest aktywny.
	// RendererLayer subskrybuje i deleguje do kamery.

	struct OrbitDelta final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	struct PanDelta final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	struct ZoomDelta final : public BusEvent
	{
		std::string windowId;
		float amount = 0.0f;
	};

	struct FocusChanged final : public BusEvent
	{
		std::string windowId;
		bool focused = false;
	};

	struct AlignToAxisRequested final : public BusEvent
	{
		std::string windowId;
		int axis = 0;
	};

	enum class OrbitDirection
	{
		Left,
		Right,
		Up,
		Down
	};

	enum class RollDirection
	{
		Left,
		Right
	};

	enum class ZoomDirection
	{
		In,
		Out
	};

	struct OrbitDirectionRequested final : public BusEvent
	{
		std::string windowId;
		OrbitDirection direction = OrbitDirection::Left;
	};

	struct OrbitQuarterTurnRequested final : public BusEvent
	{
		std::string windowId;
		OrbitDirection direction = OrbitDirection::Left;
	};

	struct RollDirectionRequested final : public BusEvent
	{
		std::string windowId;
		RollDirection direction = RollDirection::Left;
	};

	struct ZoomDirectionRequested final : public BusEvent
	{
		std::string windowId;
		ZoomDirection direction = ZoomDirection::In;
	};

	struct OrbitStepRequested final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	// Discrete keyboard pan (Shift+Arrow) - reuses OrbitDirection's 4 values rather than a new enum.
	// Distinct from PanDelta above: that one is the continuous mouse-drag path (no undo push per
	// pixel); this is a single step sized by windowState.pixelStepPx, pushed as one undo entry -
	// same instant-apply pattern as OrbitStepRequested/RollStepRequested.
	struct PanDirectionRequested final : public BusEvent
	{
		std::string windowId;
		OrbitDirection direction = OrbitDirection::Left;
	};

	struct PanStepRequested final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	struct RollStepRequested final : public BusEvent
	{
		std::string windowId;
		float delta = 0.0f;
	};

	struct ZoomStepRequested final : public BusEvent
	{
		std::string windowId;
		float amount = 0.0f;
	};

	struct FocusSelectedAtomRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct UndoViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct RedoViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct SaveCurrentViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Local per-window undo/redo for pinned measurement labels (add/remove/flip/gizmo drag) - own
	// stack and own chord (Ctrl+Alt+U / Ctrl+Alt+Shift+U), same "renderer-local, not domain" shape
	// as UndoViewRequested/RedoViewRequested above rather than the global Core/Undo stack (Ctrl+Z),
	// since pinned measurements are renderer-only state, not a domain concept - see
	// RendererWindowState::pinnedMeasurements.
	struct UndoLabelsRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct RedoLabelsRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct CycleSavedViewRequested final : public BusEvent
	{
		std::string windowId;
		int direction = 1;
	};

	struct ExportImageRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct ViewTransitionRequested final : public BusEvent
	{
		std::string windowId;
		RendererViewSnapshot targetView;
		std::string sourceAction;
	};

	struct ProjectionToggleRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct AtomSelectionRequested final : public BusEvent
	{
		std::string windowId;
		std::optional<std::size_t> atomIndex;
		bool additive = false;
	};

	// Mirrors AtomSelectionRequested for bond entities (RendererPanel's viewport click handler picks
	// atoms first, bonds only if no atom was hit - see handleViewportPick). bondIndex indexes
	// RendererStructureData::bonds (the render-side, visibility-filtered list), not the domain
	// CrystalStructure::bonds list.
	struct BondSelectionRequested final : public BusEvent
	{
		std::string windowId;
		std::optional<std::size_t> bondIndex;
		bool additive = false;
	};

	// Toggles the active drag-select tool: pressing the chord for the already-active tool turns
	// it back off (mode None), pressing the other one switches directly.
	struct SelectionToolToggleRequested final : public BusEvent
	{
		std::string windowId;
		SelectionToolMode tool = SelectionToolMode::None;
	};

	// Switches the active viewport's transform gizmo operation (G/R/S). Gizmo visibility itself
	// is driven by "selection non-empty", not by a separate flag.
	struct GizmoOperationRequested final : public BusEvent
	{
		std::string windowId;
		GizmoOperation operation = GizmoOperation::Translate;
	};

	// Toggles auto bond-length label visibility for every bond (Etap E, `Alt+M`) on the active
	// viewport.
	struct LabelsToggleRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Pins/unpins a bond-length label for every bonded pair within the current selection (>= 2
	// atoms, Etap E, `M`) - distinct from LabelsToggleRequested's "every bond in the structure"
	// toggle, and from LabelsToggleSelectedAngleRequested's angle measurement (`Shift+M`).
	struct LabelsToggleSelectedBondRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Pins/unpins the angle label for the current 3-atom selection (Etap E, `Shift+M`) - separate
	// shortcut from the bond-length one above so picking a 3rd atom for an angle doesn't also spam
	// bond-length pins for whatever pairs happen to be bonded within that triple.
	struct LabelsToggleSelectedAngleRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Full bond/angle pin key matrix (`M` family): Ctrl selects scope (selection vs. every visible
	// atom), Alt selects action (add vs. remove) on top of the base `M`/`Shift+M` (bond/angle, add,
	// selection-scoped - see above). Removal filters existing pins by atom-set containment rather
	// than recomputing bond candidates, so it also cleans up a pin left over from a structure edit
	// that no longer has a matching bond.
	struct LabelsRemoveSelectedBondRequested final : public BusEvent
	{
		std::string windowId;
	};
	struct LabelsShowAllBondRequested final : public BusEvent
	{
		std::string windowId;
	};
	struct LabelsRemoveAllBondRequested final : public BusEvent
	{
		std::string windowId;
	};
	struct LabelsRemoveSelectedAngleRequested final : public BusEvent
	{
		std::string windowId;
	};
	struct LabelsShowAllAngleRequested final : public BusEvent
	{
		std::string windowId;
	};
	struct LabelsRemoveAllAngleRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Bulk-toggles PinnedMeasurement::alignToBondDirection for every existing bond-length pin (and
	// the default new ones get created with) between "along the bond" (default) and upright (`A`).
	struct LabelsToggleBondAlignmentRequested final : public BusEvent
	{
		std::string windowId;
	};

	enum class RegionSelectMode
	{
		Replace,
		Add,
		Subtract
	};

	// Published by RendererPanel once a box/circle drag completes, with the already hit-tested
	// (and visibility-filtered) atom indices - RendererLayer only applies set semantics, it does
	// not redo any projection math.
	struct RegionSelectionRequested final : public BusEvent
	{
		std::string windowId;
		std::vector<std::size_t> atomIndices;
		RegionSelectMode mode = RegionSelectMode::Replace;
	};

	struct HideSelectionRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct ShowAllRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct SelectionInvertRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct SelectAllRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Places/moves the 3D cursor (vertical toolbar "3D point" tool click, or a context-menu "Move
	// cursor to..." item) - renderer-only state (RendererWindowState::cursor3DPosition), no domain
	// mutation, so this stays a plain event rather than a CommandRegistry ICommand.
	struct Cursor3DSetPositionRequested final : public BusEvent
	{
		std::string windowId;
		glm::vec3 position = glm::vec3(0.0f);
	};

	// Captures the target window's current view (camera + selection + visibility) as the
	// session-scoped default - see RendererLayer::m_SessionDefaultView.
	struct SetAsDefaultViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct ApplyDefaultViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Published by RendererPanel when a WAVECAR file dragged from ProjectTreePanel is dropped on
	// windowId's viewport (T08.6.4) - EditorLayer sets that window's ElectronicStructureSession
	// calculationDirectory to wavecarPath's folder and reloads. POSCAR/CONTCAR stay context-menu
	// only (ProjectTreePanel's existing "Open Defect"), no drag-drop path for those.
	struct WavecarDropped final : public BusEvent
	{
		std::string windowId;
		Path wavecarPath;
	};

} // namespace DefectStudio::RendererEvents::Viewport

namespace DefectStudio::RendererEvents::Windows
{
	// Opens filePath as a new renderer window at runtime (e.g. Project Tree "Open Defect").
	// Handled off the main thread via JobSystem - see RendererLayer::onOpenStructureRequested.
	struct OpenStructureRequested final : public BusEvent
	{
		Path filePath;
		std::string displayName;
	};
} // namespace DefectStudio::RendererEvents::Windows
