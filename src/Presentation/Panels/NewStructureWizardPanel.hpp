#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/Crystal/PrototypeDefinition.hpp"
#include "Domain/Crystal/PrototypeLoader.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class JobSystem;
	class GetSymmetryInfoJob;

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
			std::string title = "New Structure",
			bool visibleByDefault = false);
		NewStructureWizardPanel(const NewStructureWizardPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
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
		[[nodiscard]] CrystalStructure buildStructure() const;
		void drawLatticeSection();
		void drawCenteringPresetRow();
		void drawBasisTable();
		void drawElementPickerPopup(const char *popupId, std::string &targetSpecies);
		void drawSymmetrySection();
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

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;

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
	};
} // namespace DefectStudio
