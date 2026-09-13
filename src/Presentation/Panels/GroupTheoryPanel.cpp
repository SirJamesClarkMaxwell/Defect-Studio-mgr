#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryPanel.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <utility>

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Presentation/EditorFonts.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "ScientificRuntime/Python/AnalyzePointGroupJob.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr const char *kGroups[] = {
			"Detect", "C1", "Ci", "C2", "Cs", "C2h", "D2", "C2v", "D2h", "C4", "S4", "C4h", "D4",
			"C4v", "D2d", "D4h", "C3", "S6", "D3", "C3v", "D3d", "C6", "C3h", "C6h", "D6", "C6v",
			"D3h", "D6h", "T", "Th", "O", "Td", "Oh"};

		[[nodiscard]] std::vector<std::string> SplitLabels(const char *text)
		{
			std::string normalized(text);
			for (char &character : normalized)
			{
				if (character == ',')
					character = ' ';
			}

			std::istringstream stream(normalized);
			std::vector<std::string> labels;
			std::string label;
			while (stream >> label)
				labels.push_back(std::move(label));
			return labels;
		}
	} // namespace

	GroupTheoryPanel::GroupTheoryPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem))
	{
		m_ActiveIrreps[0] = '\0';
	}

	Ref<IPanel> GroupTheoryPanel::Clone() const
	{
		return CreateRef<GroupTheoryPanel>(*this);
	}

	void GroupTheoryPanel::Render()
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

		ImFont *mathFont = GetEditorMathFont();
		if (mathFont != nullptr)
			ImGui::PushFont(mathFont);

		pollJob();
		const std::string focusedWindowId = m_RendererLayer.GetLastFocusedViewportWindowId();
		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		const RendererWindowState *windowState = nullptr;
		const StructureRecord *record = nullptr;
		if (domainLayer != nullptr && !focusedWindowId.empty())
		{
			const Result<AtomEditTarget> target =
				ResolveAtomEditTarget(m_RendererLayer, *domainLayer, focusedWindowId);
			if (target)
			{
				windowState = target->windowState;
				record = target->record.get();
			}
		}

		if (windowState == nullptr || record == nullptr)
		{
			ImGui::TextDisabled("No renderer viewport focused, or the focused window has no structure.");
		}
		else
		{
			drawBasis(focusedWindowId, *windowState, *record);
			drawGroupControls();
		}

		const std::optional<BasisKey> currentKey = currentBasisKey();
		const bool stale = m_Basis.has_value() && (!currentKey.has_value() || currentKey != m_BasisKey);
		if (stale)
		{
			ImGui::TextColored(
				ImVec4(1.0f, 0.8f, 0.1f, 1.0f),
				"Stale: the structure changed since this result was computed.");
			if (ImGui::Button("Recompute") && m_PendingJob == nullptr && windowState != nullptr && record != nullptr)
				buildBasisAndSubmit(focusedWindowId, *windowState, *record);
		}

		drawError();
		if (m_Result.has_value())
			drawResults();

		if (mathFont != nullptr)
			ImGui::PopFont();
		ImGui::End();
		SetVisible(windowOpen);
	}

	void GroupTheoryPanel::drawBasis(
		const std::string &windowId,
		const RendererWindowState &windowState,
		const StructureRecord &record)
	{
		ImGui::Text(
			"Source: %s (%zu selected atoms)",
			record.displayName.c_str(),
			windowState.selectedAtomIndices.size());

		const char *centreLabel = "Selection centroid";
		if (m_CentreMode == CentreMode::Cursor)
			centreLabel = "3D cursor";
		else if (m_CentreMode == CentreMode::Atom)
			centreLabel = "Atom";

		if (ImGui::BeginCombo("Centre", centreLabel))
		{
			if (ImGui::Selectable("Selection centroid", m_CentreMode == CentreMode::SelectionCentroid))
				m_CentreMode = CentreMode::SelectionCentroid;

			ImGui::BeginDisabled(!windowState.cursor3DPlaced);
			if (ImGui::Selectable("3D cursor", m_CentreMode == CentreMode::Cursor))
				m_CentreMode = CentreMode::Cursor;
			ImGui::EndDisabled();

			if (ImGui::Selectable("Atom", m_CentreMode == CentreMode::Atom))
				m_CentreMode = CentreMode::Atom;
			ImGui::EndCombo();
		}

		ImGui::BeginDisabled(windowState.selectedAtomIndices.empty());
		if (ImGui::Button("Use selection as basis"))
			buildBasisAndSubmit(windowId, windowState, record);
		ImGui::EndDisabled();

		if (m_Basis.has_value())
		{
			ImGui::TextDisabled("Sites:");
			for (std::size_t index = 0; index < m_Basis->sites.size(); ++index)
			{
				if (index != 0)
					ImGui::SameLine(0.0f, 0.0f);
				ImGui::TextDisabled("%s%s", index == 0 ? "" : ", ", m_Basis->sites[index].label.c_str());
			}
		}
		ImGui::Separator();
	}

	void GroupTheoryPanel::drawGroupControls()
	{
		if (ImGui::BeginCombo("Point group", kGroups[m_GroupIndex]))
		{
			for (int index = 0; index < static_cast<int>(sizeof(kGroups) / sizeof(kGroups[0])); ++index)
			{
				if (ImGui::Selectable(kGroups[index], index == m_GroupIndex))
					m_GroupIndex = index;
			}
			ImGui::EndCombo();
		}

		m_Tolerance = std::max(1.0e-6, m_Tolerance);
		ImGui::InputDouble("Tolerance (Å)", &m_Tolerance, 0.0, 0.0, "%.6g");
		m_Tolerance = std::max(1.0e-6, m_Tolerance);
		ImGui::InputText("Active irreps", m_ActiveIrreps.data(), m_ActiveIrreps.size());
		ImGui::InputInt("Electrons", &m_Electrons);
		m_Electrons = std::max(0, m_Electrons);

		const bool jobRunning = m_PendingJob != nullptr;
		ImGui::BeginDisabled(!m_Basis.has_value() || jobRunning);
		if (ImGui::Button("Compute"))
			submitAnalysis();
		ImGui::EndDisabled();
		if (jobRunning)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Working...");
		}
		ImGui::Separator();
	}

	void GroupTheoryPanel::buildBasisAndSubmit(
		const std::string &windowId,
		const RendererWindowState &windowState,
		const StructureRecord &record)
	{
		if (windowState.selectedAtomIndices.empty())
			return;

		const std::size_t primary = windowState.selectedAtomIndices.back();
		if (primary >= record.structure.atoms.size())
			return;

		glm::dvec3 centre = glm::dvec3(record.structure.atoms[primary].position);
		if (m_CentreMode == CentreMode::Cursor)
			centre = glm::dvec3(windowState.cursor3DPosition);

		Result<SelectionBasis> basis =
			BuildSelectionBasis(record.structure, windowState.selectedAtomIndices, centre);
		if (!basis)
		{
			m_Error = basis.Error();
			return;
		}

		if (m_CentreMode == CentreMode::SelectionCentroid)
		{
			glm::dvec3 mean(0.0);
			for (const BasisSite &site : basis->sites)
				mean += site.position;
			centre += mean / static_cast<double>(basis->sites.size());
			basis = BuildSelectionBasis(record.structure, windowState.selectedAtomIndices, centre);
			if (!basis)
			{
				m_Error = basis.Error();
				return;
			}
		}

		m_Basis = basis.Value();
		m_BasisKey = BasisKey{windowId, ToString(record.id), record.revision, m_Basis->hash};
		m_Error.reset();
		submitAnalysis();
	}

	void GroupTheoryPanel::submitAnalysis()
	{
		if (!m_Basis.has_value())
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_Error = StructuredError{
				ErrorCategory::Job,
				Severity::Error,
				"JobSystem unavailable.",
				"",
				"",
				"GroupTheoryPanel"};
			return;
		}
		if (!m_BasisKey.has_value())
			return;

		PointGroupAnalysisRequest request;
		request.pointGroupLabel = m_GroupIndex == 0 ? "" : kGroups[m_GroupIndex];
		request.sites = m_Basis->sites;
		request.symmetryTolerance = m_Tolerance;
		request.activeOrbitalIrreps = SplitLabels(m_ActiveIrreps.data());
		request.activeElectronCount = m_Electrons;

		m_PendingJob = CreateRef<AnalyzePointGroupJob>(std::move(request));
		m_SubmittedKey = m_BasisKey;
		m_PendingJobId = jobSystem->Submit(m_PendingJob, JobPriority::Normal);
		m_Error.reset();
	}

	std::optional<GroupTheoryPanel::BasisKey> GroupTheoryPanel::currentBasisKey() const
	{
		if (!m_BasisKey.has_value())
			return std::nullopt;

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
			return std::nullopt;

		const Result<AtomEditTarget> target =
			ResolveAtomEditTarget(m_RendererLayer, *domainLayer, m_BasisKey->windowId);
		if (!target || target->record == nullptr)
			return std::nullopt;

		const StructureRecord &record = *target->record;
		if (ToString(record.id) != m_BasisKey->structureId)
			return std::nullopt;

		BasisKey current = *m_BasisKey;
		current.revision = record.revision;
		return current;
	}

	void GroupTheoryPanel::pollJob()
	{
		if (m_PendingJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			if (m_PendingJob->GetResult().has_value() && m_SubmittedKey == m_BasisKey)
			{
				m_Result = m_PendingJob->GetResult();
				m_PhysicalBuffers.assign(m_Result->reduction.projectedVectors.size(), {});
				m_VectorOrder.resize(m_Result->reduction.projectedVectors.size());
				std::iota(m_VectorOrder.begin(), m_VectorOrder.end(), 0);
				m_Error.reset();
			}
		}
		else if (m_PendingJob->GetError().has_value())
		{
			m_Error = *m_PendingJob->GetError();
		}
		else
		{
			m_Error = StructuredError{
				ErrorCategory::Job,
				Severity::Error,
				snapshot->errorMessage.empty() ? "Analysis failed." : snapshot->errorMessage,
				"",
				"",
				"GroupTheoryPanel"};
		}

		m_PendingJob.reset();
		m_PendingJobId = 0;
		m_SubmittedKey.reset();
	}

	void GroupTheoryPanel::drawError()
	{
		if (!m_Error.has_value())
			return;

		ImGui::TextColored(
			ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
			"%s",
			DescribeErrorCategory(m_Error->code).c_str());
		ImGui::TextWrapped("%s", m_Error->userMessage.c_str());
		if (!m_Error->suggestion.empty())
			ImGui::TextWrapped("%s", m_Error->suggestion.c_str());
	}
} // namespace DefectStudio
