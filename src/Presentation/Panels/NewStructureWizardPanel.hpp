#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "App/CreationSession.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/Crystal/PrototypeDefinition.hpp"
#include "Domain/Crystal/PrototypeLoader.hpp"
#include "Presentation/Panels/BondSettingsEditor.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class JobSystem;
	class EventBus;
	class GetSymmetryInfoJob;
	class OpenDefectJob;

	// "New Structure" wizard - builds a CrystalStructure from scratch (crystal system + lattice
	// parameters + an optional centering preset + a hand-edited atomic basis table) and opens it as
	// a live renderer window via OpenCrystalStructureAsWindow. This is the first of Faza 5's three
	// ImGui panels (docs/work/project/plans/2026-08-30-supercell-generation.md Task 7); Task 8
	// (SupercellBuilderPanel) and Task 9 (MaterialsCollectionPanel) follow its dependency-injection
	// shape. Task 9 may also seed this panel from an existing material (edit-then-save-as-new-entry)
	// via SeedFromStructure - not built here since Task 9 doesn't exist yet, only the state this
	// panel needs is kept simple enough to make that easy later.
	class NewStructureWizardPanel final : public IPanel
	{
	public:
		explicit NewStructureWizardPanel(
			RendererLayer &rendererLayer,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<JobSystem> jobSystem,
			ElementPropertiesTable elementPropertiesTable,
			AtomStyleTable atomStyleTable,
			Ref<CreationSessionRegistry> sessionRegistry,
			Ref<EventBus> eventBus,
			std::string title = "New Structure",
			bool visibleByDefault = false);
		NewStructureWizardPanel(const NewStructureWizardPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Retrieve the currently-built structure (if valid).
		[[nodiscard]] std::optional<CrystalStructure> GetBuiltStructure() const;

		// Last file selected in the Project Tree, forwarded by EditorLayer. Empty for a directory
		// or the project root - the "Use Project Tree selection" button is disabled then.
		void SetProjectTreeSelection(Path selectedFile);

		// Whether ui.pseudopotential_dir points anywhere. Pushed in by EditorLayer (which owns the
		// config) instead of read here, because the config object is REPLACED on every apply - a
		// reference kept by the panel would dangle. Unsets the export flag when it goes false.
		void SetPseudopotentialDirConfigured(bool configured);

	private:
		void drawModeSelector();
		// Analyze Existing / Import File: the file load runs as an OpenDefectJob, never inline in
		// Render() - parsing a POSCAR goes through a Python subprocess, which would stall the frame.
		void drawFileLoadSection();
		// NFD open-file dialog, then straight into dispatchFileLoad.
		void browseForStructureFile();
		// Copies the structure straight out of the last-focused viewport - no disk read and no
		// Python round-trip, unlike the other three routes.
		void adoptFocusedViewportStructure();
		// Fills the path field from `path` and dispatches the load. Shared by Browse, the Project
		// Tree button and the drag-and-drop target.
		void loadStructureFile(const std::string &path);
		void dispatchFileLoad();
		void pollFileLoadJob();
		// Replaces the whole form with a structure that came off disk.
		void adoptLoadedStructure(const CrystalStructure &structure);
		// Creates the session (and publishes CreationSessionCreated) the first time the draft is
		// valid, so the preview panes exist while the structure is being edited rather than only
		// after hand-off. No-op once the session exists.
		void ensureSession();
		// Submit-only: publishes SessionReadyForStructureHub, which puts the draft on the Hub's
		// list. It opens nothing - the panes are already open.
		void moveToStructureHub();
		// Keeps the registry's copy of the draft current while the user edits.
		void syncDraftToSession();

		struct BasisRow
		{
			// Empty, not "X": a default species silently puts an element nobody asked for into the
			// structure, and "X" reads as a delete affordance. Create stays disabled until every
			// row has one.
			std::string species;
			glm::vec3 fractional{0.0f};
		};

		// Derives the values EFFECTIVELY used for the currently-locked a/b/c/angle fields, in the
		// same shape BuildLatticeCell derives them internally (BravaisLattice.cpp) - used only to
		// display live "b/c/angle grayed-out fields track the free ones" feedback (checklist item 1),
		// never as an input to BuildLatticeCell itself (that function already re-derives locked
		// fields from m_Params on its own, so this and BuildLatticeCell can never disagree).
		[[nodiscard]] LatticeParameters computeEffectiveParameters() const;
		// The expanded conventional cell - lattice (x) basis, i.e. what reaches POSCAR.
		[[nodiscard]] CrystalStructure buildStructure() const;
		// The motif alone, for the basis pane and for the expansion to convolve.
		[[nodiscard]] std::vector<AtomSite> motifAtoms() const;
		[[nodiscard]] CrystalStructure buildMotifStructure() const;
		void drawLatticeSection();
		// Radio group inside the lattice section: the centering is a property of the LATTICE, so
		// picking one never touches the basis table (it used to overwrite it with lattice points,
		// which is what made every F-centred structure come out as simple cubic).
		void drawCenteringPresetRow();
		void drawBasisTable();
		// The list of what `basis (x) centering` actually produced, collapsed by default. Without it
		// the user types 2 rows and 8 atoms silently reach the renderer and POSCAR. Each row's
		// element is editable - see m_SpeciesOverrides.
		void drawGeneratedAtomsSection();
		void drawElementPickerPopup(const char *popupId, std::string &targetSpecies);
		void drawSymmetrySection();
		void drawBondSection();
		void dispatchSymmetryCheck();
		void pollSymmetryJob();
		void drawFormulaAndMappingSection();
		void drawMaterialSection();
		void applySelectedMaterial();
		void ensureCatalogLoaded();
		// Writes the selected prototype's conventional-cell basis into m_BasisRows, one row per
		// listed position, taking each row's species from the site mapping. Called on the events
		// that can change the answer (material applied, prototype switched, mapping edited) rather
		// than every frame, so it never fights a hand-edited basis table.
		void applyPrototypeToBasis();
		[[nodiscard]] const PrototypeDefinition *selectedPrototype() const;
		void drawPreviewControls();
		// Reads atom positions the gizmo moved in the session's unit-cell view back into the basis
		// table. The fields stay the single source of truth: the very next rebuild regenerates the
		// window from them, so without this a drag would simply be undone on the next keystroke.
		bool pullGizmoEditsFromPreview();
		// The session this panel is bound to, or nullptr while the draft has not been handed over.
		[[nodiscard]] Ref<CreationSession> activeSession() const;

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;
		Ref<CreationSessionRegistry> m_SessionRegistry;
		Ref<EventBus> m_EventBus;

		// The four entry modes. They differ only in how the draft gets its initial contents; from
		// "Move to Structure Hub" onwards every mode follows the identical path.
		CreationMode m_Mode = CreationMode::FromTemplate;
		// Set once this draft has been handed to the Structure Hub; until then it lives only here.
		std::optional<Uuid> m_SessionId;

		std::array<char, 512> m_LoadFilePathBuffer{};
		Path m_ProjectTreeSelection;
		Ref<OpenDefectJob> m_PendingLoadJob;
		JobId m_PendingLoadJobId = 0;
		std::string m_LoadError;
		std::string m_LoadStatus;

		std::array<char, 128> m_StructureNameBuffer{}; // filled with "New Structure" in the ctor
		CrystalSystem m_System = CrystalSystem::Cubic;
		LatticeParameters m_Params;
		std::vector<BasisRow> m_BasisRows;

		Ref<GetSymmetryInfoJob> m_PendingSymmetryJob;
		JobId m_PendingSymmetryJobId = 0;
		std::optional<SymmetryInfo> m_SymmetryResult;
		std::string m_SymmetryError;

		std::array<char, 128> m_FormulaBuffer{}; // e.g., "GaAs", "Al2O3"
		std::vector<std::string> m_SiteSpecies; // one per site of the selected prototype
		int m_SelectedPrototypeIndex = -1; // index into m_Catalog.prototypes, -1 = none

		// prototypes.yaml + materials.yaml, loaded once on first render. Previously the panel
		// carried its own hardcoded copy of the prototype list while the loader returned an empty
		// vector, so neither the YAML files nor the lattice constants in them were ever read.
		PrototypesAndMaterials m_Catalog;
		bool m_CatalogLoaded = false;
		std::string m_CatalogError;

		int m_SelectedMaterialIndex = -1; // index into m_Catalog.materials, -1 = none
		std::string m_SelectedPolytype;
		std::string m_SelectedFunctional = "exp";

		bool m_ExportPotcar = false; // POTCAR export flag (disabled if pseudodir not configured)
		bool m_PseudopotentialDirConfigured = false;

		// Per-generated-atom species, indexed like buildStructure()'s atom vector; an empty entry
		// means "keep what the basis row gave it". This is how an ordering the centering cannot
		// express gets typed in - pick F, get the four generated atoms, set them one by one -
		// without hand-writing one basis row per site. Positions are never touched.
		std::vector<std::string> m_SpeciesOverrides;


		// The three preview views live in StructureCreationTabsPanel, docked into the session's own
		// 2+1 layout. This panel only decides which of them are shown and how big the supercell is;
		// it owns no renderer window of its own (free-floating previews with a toolbar each were
		// exactly the duplicate-window problem the session tab exists to solve).
		// [0] basis (motif), [1] unit cell, [2] supercell - same order as CreationSession.
		std::array<bool, 3> m_ViewVisible{true, true, true};
		glm::ivec3 m_SupercellCounts{1, 1, 1};
		bool m_ShowPrimitiveCell = false;
		// Bond cutoffs for the draft. Here rather than in the Bond Settings panel because that one
		// edits a structure through an undoable command against its domain record, and a draft has
		// none until Add to Project registers it - the panel's own target resolution refuses a
		// preview window outright.
		BondGenerationSettings m_BondSettings;
		BondSettingsEditorState m_BondEditorState;
		// Whether the basis pane's gizmo was dragging LAST frame. pullGizmoEditsFromPreview needs it
		// because a drag clears gizmoDragActive on the frame it commits the final position, so the
		// read-back has to survive exactly one frame past the end of the drag.
		bool m_GizmoDragWasActive = false;
		// A lattice property, always known: `lattice (x) basis` needs it on every build, and the
		// primitive-cell overlay follows from it. Reading a centering back OUT of a hand-edited
		// basis would be an spglib job, which is why it is chosen here rather than inferred.
		BravaisCenteringPreset m_Centering = BravaisCenteringPreset::Primitive;
	};
} // namespace DefectStudio
