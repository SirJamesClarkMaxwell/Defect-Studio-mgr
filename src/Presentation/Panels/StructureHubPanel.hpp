#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class JobSystem;
	class NewStructureWizardPanel;

	// Unified Structure Hub: three entry modes (Create New, From Library, Import File)
	// all converge on a single "Add to project" workflow. Replaces NewStructureWizardPanel
	// and MaterialsCollectionPanel. Listen to ProjectTreeSelectionChanged to populate
	// target directory selector.
	class StructureHubPanel final : public IPanel
	{
	public:
		explicit StructureHubPanel(
			RendererLayer &rendererLayer,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<JobSystem> jobSystem,
			ElementPropertiesTable elementPropertiesTable,
			AtomStyleTable atomStyleTable,
			Path personalLibraryPath,
			std::string title = "Structure Hub",
			bool visibleByDefault = false);
		StructureHubPanel(const StructureHubPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Called when ProjectTreeSelectionChanged fires; sets the target directory for "Add to project"
		void SetTargetDirectory(const Path &targetDirectory);

	private:
		enum class Mode
		{
			Create,   // Build structure from scratch (reuse wizard)
			Library,  // Select from library (reuse materials panel)
			Import    // Load from file
		};

		void drawModeTab(Mode mode);
		void drawCreateTab();
		void drawLibraryTab();
		void drawImportTab();
		void drawAddToProjectSection();

		// Validates that a structure and target directory are set, publishes AddStructureToProjectRequested
		void dispatchAddToProject();

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;
		Path m_PersonalLibraryPath;

		Mode m_SelectedMode = Mode::Create;
		Path m_TargetDirectory; // Set by ProjectTreeSelectionChanged listener

		// Create mode: wizard sub-panel (lazy-init on first render)
		Ref<NewStructureWizardPanel> m_WizardPanel;
		std::optional<CrystalStructure> m_CreatedStructure;

		// Library mode: selected from materials panel
		std::optional<CrystalStructure> m_SelectedStructure;

		// Import mode: file path input
		std::array<char, 512> m_ImportFilePathBuffer{};
		std::optional<CrystalStructure> m_ImportedStructure;

		// Common
		std::array<char, 128> m_StructureNameBuffer{}; // User-provided name for the structure
		std::string m_StatusMessage;
		std::string m_ErrorMessage;
	};
} // namespace DefectStudio
