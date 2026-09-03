#include "Core/dspch.hpp"

#include "Presentation/Panels/MaterialsCollectionPanel.hpp"

#include <cstring>
#include <utility>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"

namespace DefectStudio
{
	MaterialsCollectionPanel::MaterialsCollectionPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		Path personalLibraryPath,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_PersonalLibraryPath(std::move(personalLibraryPath))
	{
	}

	Ref<IPanel> MaterialsCollectionPanel::Clone() const
	{
		return CreateRef<MaterialsCollectionPanel>(*this);
	}

	void MaterialsCollectionPanel::SetProjectLibraryPath(Path projectLibraryPath)
	{
		if (m_ProjectLibraryPath == projectLibraryPath.Native())
			return;

		m_ProjectLibraryPath = std::move(projectLibraryPath);
		m_ProjectState = ScopeState{};
	}

	Path MaterialsCollectionPanel::libraryPathFor(Scope scope) const
	{
		return scope == Scope::Project ? m_ProjectLibraryPath : m_PersonalLibraryPath;
	}

	MaterialsCollectionPanel::ScopeState &MaterialsCollectionPanel::stateFor(Scope scope)
	{
		return scope == Scope::Project ? m_ProjectState : m_PersonalState;
	}

	void MaterialsCollectionPanel::reload(Scope scope)
	{
		ScopeState &state = stateFor(scope);
		state.loaded = true;
		state.entries.clear();
		state.error.clear();

		const Path libraryPath = libraryPathFor(scope);
		if (libraryPath.Empty())
		{
			state.error = "No library path configured for this scope.";
			return;
		}

		const MaterialLibraryIO library(libraryPath);
		Result<std::vector<MaterialLibraryEntry>> listed = library.ListMaterials();
		if (!listed)
		{
			state.error = listed.Error().userMessage;
			DS_LOG_WARN("Materials Collection: listing '{}' failed: {}",
				libraryPath.String(),
				listed.Error().technicalDetails);
			return;
		}

		state.entries = std::move(listed).Value();
	}

	void MaterialsCollectionPanel::Render()
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

		if (ImGui::Button("Save current structure..."))
		{
			m_SaveNameBuffer.fill('\0');
			m_SaveNotesBuffer.fill('\0');
			m_SaveError.clear();
			m_SaveScope = m_ProjectLibraryPath.Empty() ? Scope::Personal : Scope::Project;
			m_SavePopupOpen = true;
			ImGui::OpenPopup("Save material");
		}

		if (!m_StatusMessage.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", m_StatusMessage.c_str());
		}

		drawSaveCurrentPopup();
		drawDeleteConfirmPopup();

		ImGui::Separator();

		if (ImGui::BeginTabBar("##materials_scopes"))
		{
			if (ImGui::BeginTabItem("This Project"))
			{
				drawScopeTab(Scope::Project);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("My Library"))
			{
				drawScopeTab(Scope::Personal);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
		SetVisible(windowOpen);
	}

	void MaterialsCollectionPanel::drawScopeTab(Scope scope)
	{
		const Path libraryPath = libraryPathFor(scope);
		if (libraryPath.Empty())
		{
			ImGui::TextWrapped("No project is open, so there is no project-scoped material library.");
			return;
		}

		ScopeState &state = stateFor(scope);
		if (!state.loaded)
			reload(scope);

		if (ImGui::Button("Refresh"))
			reload(scope);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", libraryPath.String().c_str());

		if (!state.error.empty())
		{
			ImGui::TextWrapped("%s", state.error.c_str());
			return;
		}

		if (state.entries.empty())
		{
			ImGui::TextDisabled("No materials saved here yet.");
			return;
		}

		constexpr ImGuiTableFlags tableFlags =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
		if (!ImGui::BeginTable("##materials", 4, tableFlags))
			return;

		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Formula");
		ImGui::TableSetupColumn("Notes");
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableHeadersRow();

		for (const MaterialLibraryEntry &entry : state.entries)
		{
			ImGui::TableNextRow();
			ImGui::PushID(entry.id.c_str());

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(entry.name.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(entry.reducedFormula.c_str());
			ImGui::TableNextColumn();
			ImGui::TextWrapped("%s", entry.notes.c_str());

			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Open"))
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				const MaterialLibraryIO library(libraryPath);
				Result<CrystalStructure> loaded = library.LoadMaterial(entry.id);
				if (!loaded)
				{
					m_StatusMessage = loaded.Error().userMessage;
					DS_LOG_WARN("Materials Collection: loading '{}' failed: {}",
						entry.name,
						loaded.Error().technicalDetails);
				}
				else if (domainLayer != nullptr)
				{
					OpenCrystalStructureAsWindow(
						std::move(loaded).Value(),
						entry.name,
						*domainLayer,
						m_RendererLayer,
						m_ElementPropertiesTable,
						m_AtomStyleTable);
					m_StatusMessage = "Opened " + entry.name;
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete"))
			{
				m_DeleteScope = scope;
				m_DeleteEntryId = entry.id;
				m_DeleteEntryName = entry.name;
				m_DeletePopupOpen = true;
				ImGui::OpenPopup("Delete material");
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	void MaterialsCollectionPanel::drawSaveCurrentPopup()
	{
		if (!ImGui::BeginPopupModal("Save material", &m_SavePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::InputText("Name", m_SaveNameBuffer.data(), m_SaveNameBuffer.size());
		ImGui::InputText("Notes", m_SaveNotesBuffer.data(), m_SaveNotesBuffer.size());

		const bool projectAvailable = !m_ProjectLibraryPath.Empty();
		ImGui::BeginDisabled(!projectAvailable);
		if (ImGui::RadioButton("This Project", m_SaveScope == Scope::Project))
			m_SaveScope = Scope::Project;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::RadioButton("My Library", m_SaveScope == Scope::Personal))
			m_SaveScope = Scope::Personal;

		if (!m_SaveError.empty())
			ImGui::TextWrapped("%s", m_SaveError.c_str());

		const bool nameEmpty = m_SaveNameBuffer[0] == '\0';
		ImGui::BeginDisabled(nameEmpty);
		if (ImGui::Button("Save"))
		{
			m_SaveError.clear();

			Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
			Ref<StructureRecord> sourceRecord;
			const std::string &focusedWindowId = m_RendererLayer.GetLastFocusedViewportWindowId();
			if (domainLayer != nullptr && !focusedWindowId.empty())
			{
				Result<AtomEditTarget> target =
					ResolveAtomEditTarget(m_RendererLayer, *domainLayer, focusedWindowId);
				if (target)
					sourceRecord = target->record;
			}

			if (sourceRecord == nullptr)
			{
				m_SaveError = "No renderer viewport focused, or the focused window has no structure.";
			}
			else
			{
				const MaterialLibraryIO library(libraryPathFor(m_SaveScope));
				Result<MaterialLibraryEntry> added = library.AddMaterial(
					sourceRecord->structure,
					m_SaveNameBuffer.data(),
					m_SaveNotesBuffer.data());

				if (!added)
				{
					m_SaveError = added.Error().userMessage;
					DS_LOG_WARN("Materials Collection: saving '{}' failed: {}",
						m_SaveNameBuffer.data(),
						added.Error().technicalDetails);
				}
				else
				{
					m_StatusMessage = "Saved " + added.Value().name;
					reload(m_SaveScope);
					m_SavePopupOpen = false;
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_SavePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void MaterialsCollectionPanel::drawDeleteConfirmPopup()
	{
		if (!ImGui::BeginPopupModal("Delete material", &m_DeletePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::Text("Delete '%s'? This cannot be undone.", m_DeleteEntryName.c_str());

		// Cancel first, so a stray Enter does not confirm a destructive action - same ordering
		// ProjectTreePanel's delete modal uses.
		if (ImGui::Button("Cancel"))
		{
			m_DeletePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete"))
		{
			const MaterialLibraryIO library(libraryPathFor(m_DeleteScope));
			Result<void> removed = library.RemoveMaterial(m_DeleteEntryId);
			if (!removed)
			{
				m_StatusMessage = removed.Error().userMessage;
				DS_LOG_WARN("Materials Collection: deleting '{}' failed: {}",
					m_DeleteEntryName,
					removed.Error().technicalDetails);
			}
			else
			{
				m_StatusMessage = "Deleted " + m_DeleteEntryName;
				reload(m_DeleteScope);
			}

			m_DeletePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
} // namespace DefectStudio
