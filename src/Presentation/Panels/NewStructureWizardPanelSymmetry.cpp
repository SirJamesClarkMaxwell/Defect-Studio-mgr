// The spglib symmetry check (spacegroup / Wyckoff readout) - a job dispatch plus its polling
// and readout. Split out of NewStructureWizardPanel.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "ScientificRuntime/Python/GetSymmetryInfoJob.hpp"

namespace DefectStudio
{
	void NewStructureWizardPanel::dispatchSymmetryCheck()
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_SymmetryError = "JobSystem unavailable";
			return;
		}
		m_PendingSymmetryJob = CreateRef<GetSymmetryInfoJob>(buildStructure(), 0.01f);
		m_PendingSymmetryJobId = jobSystem->Submit(m_PendingSymmetryJob, JobPriority::Normal);
		m_SymmetryError.clear();
		m_SymmetryResult.reset();
	}

	void NewStructureWizardPanel::pollSymmetryJob()
	{
		if (m_PendingSymmetryJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingSymmetryJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			m_SymmetryResult = m_PendingSymmetryJob->GetResult();
			if (!m_SymmetryResult.has_value())
				m_SymmetryError = "Symmetry check completed with no result";
			else
				m_SymmetryError.clear();
		}
		else
		{
			m_SymmetryError = snapshot->errorMessage.empty() ? "Symmetry check failed" : snapshot->errorMessage;
		}
		m_PendingSymmetryJob.reset();
		m_PendingSymmetryJobId = 0;
	}

	void NewStructureWizardPanel::drawSymmetrySection()
	{
		const bool loading = m_PendingSymmetryJob != nullptr;
		ImGui::BeginDisabled(loading || m_BasisRows.empty());
		if (ImGui::Button("Show symmetry"))
			dispatchSymmetryCheck();
		ImGui::EndDisabled();
		if (loading)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Checking...");
		}
		if (!m_SymmetryError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_SymmetryError.c_str());

		if (m_SymmetryResult.has_value())
		{
			ImGui::Text("Spacegroup: %d (%s)", m_SymmetryResult->spacegroupNumber, m_SymmetryResult->spacegroupSymbol.c_str());
			ImGui::Text("Point group: %s", m_SymmetryResult->pointGroupSymbol.c_str());
			if (ImGui::TreeNode("Wyckoff positions"))
			{
				for (std::size_t i = 0; i < m_SymmetryResult->wyckoffLetters.size(); ++i)
				{
					const std::string species = i < m_BasisRows.size() ? m_BasisRows[i].species : "?";
					ImGui::Text("Atom %zu (%s): %s", i, species.c_str(), m_SymmetryResult->wyckoffLetters[i].c_str());
				}
				ImGui::TreePop();
			}
		}
	}
} // namespace DefectStudio
