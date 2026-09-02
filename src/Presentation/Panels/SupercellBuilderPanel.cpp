#include "Core/dspch.hpp"

#include "Presentation/Panels/SupercellBuilderPanel.hpp"

#include <optional>
#include <utility>

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "ScientificRuntime/Python/SuggestSupercellMatrixJob.hpp"

namespace DefectStudio
{
	SupercellBuilderPanel::SupercellBuilderPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable))
	{
	}

	Ref<IPanel> SupercellBuilderPanel::Clone() const
	{
		return CreateRef<SupercellBuilderPanel>(*this);
	}

	void SupercellBuilderPanel::Render()
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

		pollSurfaceJob();

		// GetLastFocusedViewportWindowId, not GetFocusedViewportWindowId - the latter clears the
		// instant ImGui focus moves into this panel's own fields (same reasoning as
		// BondSettingsPanel).
		const std::string &focusedWindowId = m_RendererLayer.GetLastFocusedViewportWindowId();
		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		Ref<StructureRecord> sourceRecord;
		if (domainLayer != nullptr && !focusedWindowId.empty())
		{
			Result<AtomEditTarget> target = ResolveAtomEditTarget(m_RendererLayer, *domainLayer, focusedWindowId);
			if (target)
				sourceRecord = target->record;
		}

		if (sourceRecord == nullptr)
		{
			ImGui::TextDisabled("No renderer viewport focused, or the focused window has no structure.");
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (m_EditedForWindowId != focusedWindowId)
		{
			m_EditedForWindowId = focusedWindowId;
			m_EditedMatrix = SupercellMatrix{};
			m_SurfaceError.clear();
			m_StatusMessage.clear();
		}

		ImGui::Text("Source: %s", sourceRecord->displayName.c_str());
		ImGui::TextDisabled("%zu atoms in the unit cell", sourceRecord->structure.atoms.size());
		ImGui::Separator();

		if (ImGui::BeginTabBar("##supercell_modes"))
		{
			if (ImGui::BeginTabItem("Simple"))
			{
				drawSimpleTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Matrix"))
			{
				drawMatrixTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Surface"))
			{
				drawSurfaceTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();

		const int determinant = m_EditedMatrix.Determinant();
		const std::size_t unitAtoms = sourceRecord->structure.atoms.size();
		if (determinant > 0)
		{
			ImGui::Text("Resulting atom count: %zu", unitAtoms * static_cast<std::size_t>(determinant));
		}
		else
		{
			ImGui::TextDisabled("Resulting atom count: n/a");
			ImGui::TextWrapped(
				"Determinant is %d - a supercell needs a positive determinant (it is the atom-count "
				"multiplier). Fix the matrix in the Matrix tab.",
				determinant);
		}

		ImGui::BeginDisabled(determinant <= 0);
		if (ImGui::Button("Generate"))
		{
			Result<CrystalStructure> supercell = BuildSupercell(sourceRecord->structure, m_EditedMatrix);
			if (!supercell)
			{
				m_StatusMessage = supercell.Error().userMessage;
				DS_LOG_WARN("Supercell generation failed for '{}': {}",
					sourceRecord->displayName,
					supercell.Error().technicalDetails);
			}
			else
			{
				const std::string generatedName =
					sourceRecord->displayName + " x" + std::to_string(determinant);
				OpenCrystalStructureAsWindow(
					std::move(supercell).Value(),
					generatedName,
					*domainLayer,
					m_RendererLayer,
					m_ElementPropertiesTable,
					m_AtomStyleTable,
					/*showCellBox=*/true,
					/*showGrid=*/true);
				m_StatusMessage = "Opened " + generatedName;
			}
		}
		ImGui::EndDisabled();

		if (!m_StatusMessage.empty())
		{
			ImGui::SameLine();
			ImGui::TextWrapped("%s", m_StatusMessage.c_str());
		}

		ImGui::End();
		SetVisible(windowOpen);
	}

	void SupercellBuilderPanel::drawSimpleTab()
	{
		if (!m_EditedMatrix.IsDiagonal())
		{
			ImGui::TextWrapped(
				"The current matrix is not diagonal, so it cannot be shown as N x M x K. Edit it in "
				"the Matrix tab, or reset it here.");
			if (ImGui::Button("Reset to diagonal"))
				m_EditedMatrix = SupercellMatrix::Diagonal(1, 1, 1);
			return;
		}

		int diagonal[3] = {m_EditedMatrix.rows[0].x, m_EditedMatrix.rows[1].y, m_EditedMatrix.rows[2].z};
		if (ImGui::InputInt3("N x M x K##simple_diagonal", diagonal))
			m_EditedMatrix = SupercellMatrix::Diagonal(diagonal[0], diagonal[1], diagonal[2]);
	}

	void SupercellBuilderPanel::drawMatrixTab()
	{
		ImGui::TextDisabled("Rows are the new lattice vectors in terms of the old ones.");
		for (int row = 0; row < 3; ++row)
		{
			ImGui::PushID(row);
			int values[3] = {m_EditedMatrix.rows[row].x, m_EditedMatrix.rows[row].y, m_EditedMatrix.rows[row].z};
			if (ImGui::InputInt3("##matrix_row", values))
				m_EditedMatrix.rows[row] = glm::ivec3(values[0], values[1], values[2]);
			ImGui::PopID();
		}
	}

	void SupercellBuilderPanel::drawSurfaceTab()
	{
		int hkl[3] = {m_SurfaceHkl.h, m_SurfaceHkl.k, m_SurfaceHkl.l};
		if (ImGui::InputInt3("Miller (h k l)##surface_hkl", hkl))
		{
			m_SurfaceHkl.h = hkl[0];
			m_SurfaceHkl.k = hkl[1];
			m_SurfaceHkl.l = hkl[2];
		}
		ImGui::InputInt("Layers##surface_layers", &m_SurfaceLayers);

		const bool jobRunning = m_PendingSurfaceJob != nullptr;
		ImGui::BeginDisabled(jobRunning || m_SurfaceLayers < 1);
		if (ImGui::Button("Suggest"))
		{
			Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
			if (domainLayer != nullptr)
			{
				Result<AtomEditTarget> target =
					ResolveAtomEditTarget(m_RendererLayer, *domainLayer, m_EditedForWindowId);
				if (target)
					dispatchSurfaceSuggestion(target->record->structure);
			}
		}
		ImGui::EndDisabled();

		if (jobRunning)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Working...");
		}

		if (!m_SurfaceError.empty())
			ImGui::TextWrapped("%s", m_SurfaceError.c_str());
	}

	void SupercellBuilderPanel::dispatchSurfaceSuggestion(const CrystalStructure &unitCell)
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_SurfaceError = "JobSystem unavailable";
			return;
		}

		m_PendingSurfaceJob = CreateRef<SuggestSupercellMatrixJob>(unitCell, m_SurfaceHkl, m_SurfaceLayers);
		m_PendingSurfaceJobId = jobSystem->Submit(m_PendingSurfaceJob, JobPriority::Normal);
		m_SurfaceError.clear();
	}

	void SupercellBuilderPanel::pollSurfaceJob()
	{
		if (m_PendingSurfaceJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingSurfaceJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			const std::optional<SupercellMatrix> &result = m_PendingSurfaceJob->GetResult();
			if (result.has_value())
			{
				m_EditedMatrix = *result;
				m_SurfaceError.clear();
			}
			else
			{
				m_SurfaceError = "Surface suggestion completed with no result";
			}
		}
		else
		{
			m_SurfaceError = snapshot->errorMessage.empty() ? "Surface suggestion failed" : snapshot->errorMessage;
		}

		m_PendingSurfaceJob.reset();
		m_PendingSurfaceJobId = 0;
	}
} // namespace DefectStudio
