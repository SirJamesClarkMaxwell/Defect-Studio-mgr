#include "Core/dspch.hpp"

#include "Presentation/Panels/StructureHubPanel.hpp"

#include <cstring>
#include <utility>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Presentation/Panels/MaterialsCollectionPanel.hpp"
#include "Presentation/Panels/NewStructureWizardPanel.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "ScientificRuntime/Python/PuntukasBridge.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"

namespace DefectStudio
{
	StructureHubPanel::StructureHubPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		Path personalLibraryPath,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_PersonalLibraryPath(std::move(personalLibraryPath)),
		  m_EventBus(std::move(eventBus))
	{
		std::fill(m_StructureNameBuffer.begin(), m_StructureNameBuffer.end(), '\0');
		m_StructureNameBuffer[0] = 'U';
		m_StructureNameBuffer[1] = 'n';
		m_StructureNameBuffer[2] = 't';
		m_StructureNameBuffer[3] = 'i';
		m_StructureNameBuffer[4] = 't';
		m_StructureNameBuffer[5] = 'l';
		m_StructureNameBuffer[6] = 'e';
		m_StructureNameBuffer[7] = 'd';
	}

	Ref<IPanel> StructureHubPanel::Clone() const
	{
		return CreateRef<StructureHubPanel>(*this);
	}

	void StructureHubPanel::SetTargetDirectory(const Path &targetDirectory)
	{
		m_TargetDirectory = targetDirectory;
	}

	void StructureHubPanel::Render()
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

		// Status messages
		if (!m_StatusMessage.empty())
		{
			ImGui::TextColored({0.0f, 1.0f, 0.0f, 1.0f}, "%s", m_StatusMessage.c_str());
		}
		if (!m_ErrorMessage.empty())
		{
			ImGui::TextColored({1.0f, 0.0f, 0.0f, 1.0f}, "Error: %s", m_ErrorMessage.c_str());
		}

		// Tab bar for three entry modes
		if (ImGui::BeginTabBar("##structure_hub_modes"))
		{
			if (ImGui::BeginTabItem("Create New"))
			{
				drawCreateTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("From Library"))
			{
				drawLibraryTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Import File"))
			{
				drawImportTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		drawAddToProjectSection();

		ImGui::End();
		SetVisible(windowOpen);
	}

	void StructureHubPanel::drawCreateTab()
	{
		ImGui::TextWrapped(
			"Not yet implemented. Use 'New Structure' panel for now, then add the result to the project via 'Add Existing' or save it to the library first.");
	}

	void StructureHubPanel::drawLibraryTab()
	{
		ImGui::TextWrapped("Material library structures are available in the separate 'Materials Collection' panel. To add a structure from your library:");
		ImGui::BulletText("Open the Materials Collection panel (if not visible, enable it via View menu)");
		ImGui::BulletText("Click 'Open' to preview a structure");
		ImGui::BulletText("Return here and select the structure in the 'Use Library Entry' field below");
		ImGui::BulletText("Click 'Add to Project'");

		ImGui::Separator();
		ImGui::TextDisabled("(Full Create/Library mode integration deferred to Step 10b)");
	}

	void StructureHubPanel::drawImportTab()
	{
		ImGui::TextUnformatted("File path:");
		ImGui::InputText("##import_path", m_ImportFilePathBuffer.data(), m_ImportFilePathBuffer.size());

		ImGui::SameLine();
		if (ImGui::Button("Browse"))
		{
			ImGui::OpenPopup("##import_file_dialog");
		}

		// TODO: actual file dialog (would use platform-specific API or simple filename input for now)
		ImGui::TextDisabled("(Full path to .vasp, .poscar, or other structure file)");

		if (ImGui::Button("Load"))
		{
			const std::string filePath(m_ImportFilePathBuffer.data());
			if (filePath.empty())
			{
				m_ErrorMessage = "Please enter a file path.";
				return;
			}

			// Load via PuntukasBridge
			PuntukasBridge bridge;
			Result<PymatgenStructureData> loaded = bridge.LoadStructure(Path::FromResolved(filePath));
			if (!loaded)
			{
				m_ErrorMessage = loaded.Error().userMessage;
				DS_LOG_WARN("Structure Hub: import failed: {}", loaded.Error().technicalDetails);
			}
			else
			{
				m_ImportedStructure = ConvertPymatgenStructureToCrystalStructure(loaded.Value());
				m_StatusMessage = "Loaded structure from " + filePath;
				m_ErrorMessage.clear();
			}
		}

		if (m_ImportedStructure)
		{
			ImGui::Separator();
			ImGui::Text("Loaded: %zu atoms", m_ImportedStructure->atoms.size());
			if (ImGui::Button("Preview##import"))
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				if (domainLayer != nullptr)
				{
					OpenCrystalStructureAsWindow(
						m_ImportedStructure.value(),
						"Imported Structure",
						*domainLayer,
						m_RendererLayer,
						m_ElementPropertiesTable,
						m_AtomStyleTable);
				}
			}
		}
	}

	void StructureHubPanel::drawAddToProjectSection()
	{
		ImGui::TextUnformatted("Structure Name:");
		ImGui::InputText("##structure_name", m_StructureNameBuffer.data(), m_StructureNameBuffer.size());

		ImGui::TextUnformatted("Target Directory:");
		if (m_TargetDirectory.Empty())
		{
			ImGui::TextWrapped("Click a directory in the Project Tree to select where to add this structure.");
		}
		else
		{
			ImGui::TextWrapped("%s", m_TargetDirectory.String().c_str());
		}

		const bool hasStructure = m_ImportedStructure.has_value(); // TODO: check other modes too
		const bool hasTarget = !m_TargetDirectory.Empty();
		const bool hasName = m_StructureNameBuffer[0] != '\0';

		ImGui::BeginDisabled(!hasStructure || !hasTarget || !hasName);
		if (ImGui::Button("Add to Project", {-1, 0}))
		{
			dispatchAddToProject();
		}
		ImGui::EndDisabled();
	}

	void StructureHubPanel::dispatchAddToProject()
	{
		// Collect the structure from whichever mode is active
		std::optional<CrystalStructure> structure;
		switch (m_SelectedMode)
		{
			case Mode::Create:
				structure = m_CreatedStructure;
				break;
			case Mode::Library:
				structure = m_SelectedStructure;
				break;
			case Mode::Import:
				structure = m_ImportedStructure;
				break;
		}

		if (!structure)
		{
			m_ErrorMessage = "No structure selected. Create, select from library, or import a file first.";
			return;
		}

		if (m_TargetDirectory.Empty())
		{
			m_ErrorMessage = "No target directory selected. Click a directory in the Project Tree.";
			return;
		}

		const std::string displayName(m_StructureNameBuffer.data());
		if (displayName.empty())
		{
			m_ErrorMessage = "Please enter a structure name.";
			return;
		}

		// Publish AddStructureToProjectRequested event - coordinator will handle persistence + project registration
		DomainEvents::AddStructureToProjectRequested event;
		event.structure = structure.value();
		event.displayName = displayName;
		event.targetDirectory = m_TargetDirectory.Native();
		m_EventBus->Publish(event);

		m_StatusMessage = "Structure submitted to project. Coordinator will finalize add-to-project flow.";
		m_ErrorMessage.clear();

		// Clear the form for the next structure
		m_StructureNameBuffer.fill('\0');
		m_CreatedStructure.reset();
		m_SelectedStructure.reset();
		m_ImportedStructure.reset();
	}
} // namespace DefectStudio
