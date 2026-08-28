#include "Core/dspch.hpp"

#include "Presentation/Panels/DisplacementComparisonPanel.hpp"

#include <algorithm>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Domain/DomainLayer.hpp"
#include "Events/ProjectEvents.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/CompareStructuresJob.hpp"

namespace DefectStudio
{
	namespace
	{
		// RendererLayer::findWindowById is private (internal to Renderer/*) - external callers use
		// the public GetWindows() list directly, same pattern RendererAtomEditCommands.cpp's
		// ResolveAtomEditTarget already uses.
		[[nodiscard]] RendererWindowState *FindWindowById(RendererLayer &layer, const std::string &windowId)
		{
			if (windowId.empty())
				return nullptr;
			for (RendererWindowState &candidate : layer.GetWindows())
				if (candidate.windowId == windowId)
					return &candidate;
			return nullptr;
		}
	} // namespace

	DisplacementComparisonPanel::DisplacementComparisonPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		Ref<EventBus> eventBus,
		ElementPropertiesTable elementPropertiesTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_EventBus(std::move(eventBus)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable))
	{
	}

	Ref<IPanel> DisplacementComparisonPanel::Clone() const
	{
		return CreateRef<DisplacementComparisonPanel>(*this);
	}

	void DisplacementComparisonPanel::SetPersistedState(Path comparisonFilePath, float thresholdAngstrom)
	{
		m_ComparisonFilePath = std::move(comparisonFilePath);
		m_PersistedThresholdAngstrom = thresholdAngstrom;
	}

	void DisplacementComparisonPanel::SetComparisonFile(Path filePath)
	{
		m_ComparisonFilePath = std::move(filePath);
		m_Error.clear();
		SetVisible(true);
		publishPersistedStateChanged();
	}

	void DisplacementComparisonPanel::OpenForWindow(const std::string &windowId)
	{
		m_TargetWindowId = windowId;
		m_Error.clear();
		SetVisible(true);
	}

	void DisplacementComparisonPanel::dispatchCompare()
	{
		// An explicit binding (ProjectTreePanel-triggered SetComparisonFile leaves this alone, but
		// the vertical-toolbar OpenForWindow sets it) wins over re-deriving from current ImGui focus -
		// see the class comment on why this stays sticky instead of re-resolving every Compare.
		const std::string referenceWindowId =
			m_TargetWindowId.empty() ? m_RendererLayer.GetLastFocusedViewportWindowId() : m_TargetWindowId;
		if (referenceWindowId.empty())
		{
			m_Error = "No renderer viewport is focused - open/focus the reference structure first.";
			return;
		}
		RendererWindowState *windowState = FindWindowById(m_RendererLayer, referenceWindowId);
		if (windowState == nullptr || windowState->structure.domainStructureId.empty())
		{
			m_Error = "Focused window has no editable structure.";
			return;
		}
		const std::optional<Uuid> structureId = ParseUuid(windowState->structure.domainStructureId);
		if (!structureId.has_value())
		{
			m_Error = "Focused window's structure id is invalid.";
			return;
		}
		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
		{
			m_Error = "DomainLayer unavailable";
			return;
		}
		Ref<const StructureRecord> record = domainLayer->Workspace().Structures().Find(*structureId).lock();
		if (record == nullptr)
		{
			m_Error = "Focused window's structure is no longer registered.";
			return;
		}
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_Error = "JobSystem unavailable";
			return;
		}

		m_TargetWindowId = referenceWindowId;
		m_PendingJob = CreateRef<CompareStructuresJob>(record->structure, m_ComparisonFilePath, m_ElementPropertiesTable);
		m_PendingJobId = jobSystem->Submit(m_PendingJob, JobPriority::Normal);
		m_Error.clear();
	}

	void DisplacementComparisonPanel::pollJob()
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
			const std::optional<StructureComparisonResult> &result = m_PendingJob->GetResult();
			RendererWindowState *windowState = FindWindowById(m_RendererLayer, m_TargetWindowId);
			if (result.has_value() && windowState != nullptr)
			{
				float maxMagnitude = 0.0f;
				for (const AtomDisplacement &displacement : result->matches)
					maxMagnitude = std::max(maxMagnitude, displacement.magnitudeAngstrom);

				RendererWindowState::DisplacementComparisonState state;
				state.comparisonFilePath = m_ComparisonFilePath;
				state.displayThresholdAngstrom =
					(m_PersistedThresholdAngstrom.has_value() && *m_PersistedThresholdAngstrom <= maxMagnitude)
					? *m_PersistedThresholdAngstrom
					: maxMagnitude;
				state.visible = true;
				state.result = *result;
				windowState->displacementComparison = std::move(state);
				m_PersistedThresholdAngstrom.reset();
				m_Error.clear();
				publishPersistedStateChanged();
			}
			else if (!result.has_value())
			{
				m_Error = "Comparison completed with no result";
			}
			else
			{
				m_Error = "Reference window closed before the comparison finished";
			}
		}
		else
		{
			m_Error = snapshot->errorMessage.empty() ? "Comparison failed" : snapshot->errorMessage;
		}
		m_PendingJob.reset();
		m_PendingJobId = 0;
	}

	void DisplacementComparisonPanel::publishPersistedStateChanged()
	{
		if (m_EventBus == nullptr)
			return;

		RendererWindowState *windowState = FindWindowById(m_RendererLayer, m_TargetWindowId);

		ProjectEvents::DisplacementComparisonStateChanged event;
		event.comparisonFilePath = m_ComparisonFilePath;
		event.thresholdAngstrom =
			windowState != nullptr && windowState->displacementComparison.has_value()
			? windowState->displacementComparison->displayThresholdAngstrom
			: 0.0f;
		m_EventBus->Queue(event);
	}

	void DisplacementComparisonPanel::Render()
	{
		if (!IsVisible())
			return;

		ImGui::SetNextWindowSize(ImVec2(420.0f, 320.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &stillOpen))
		{
			ImGui::End();
			return;
		}
		if (!stillOpen)
		{
			SetVisible(false);
			ImGui::End();
			return;
		}

		pollJob();

		RendererWindowState *targetWindowState =
			m_TargetWindowId.empty() ? nullptr : FindWindowById(m_RendererLayer, m_TargetWindowId);

		std::string referenceLabel = "(focus a renderer viewport first)";
		if (targetWindowState != nullptr)
		{
			referenceLabel = targetWindowState->title;
		}
		else
		{
			const std::string focusedWindowId = m_RendererLayer.GetLastFocusedViewportWindowId();
			if (RendererWindowState *focusedWindow =
					focusedWindowId.empty() ? nullptr : FindWindowById(m_RendererLayer, focusedWindowId))
				referenceLabel = focusedWindow->title + " (focused, not yet compared)";
		}
		ImGui::TextDisabled("Reference: %s", referenceLabel.c_str());

		ImGui::TextUnformatted("Comparison file:");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", m_ComparisonFilePath.Empty() ? "(none)" : m_ComparisonFilePath.String().c_str());
		if (ImGui::Button("Browse..."))
		{
			// Arms ProjectTreePanel's file-pick mode (arrow keys + Enter/click, no OS dialog) - see
			// 2026-08-28 feedback. SetComparisonFile() lands via DisplacementComparisonFilePicked once
			// the user confirms a file there.
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectEvents::DisplacementComparisonFilePickRequested{});
		}
		ImGui::SameLine();
		if (ImGui::Button("OS dialog..."))
		{
			Result<std::optional<Path>> picked = Platform::PickOpenFile(
				m_ComparisonFilePath.parent_path(), "Structure files", "*");
			if (picked && picked->has_value())
			{
				m_ComparisonFilePath = picked->value();
				publishPersistedStateChanged();
			}
		}

		const bool loading = m_PendingJob != nullptr;
		ImGui::BeginDisabled(loading || m_ComparisonFilePath.Empty());
		if (ImGui::Button("Compare"))
			dispatchCompare();
		ImGui::EndDisabled();
		if (loading)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Comparing...");
		}
		if (!m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());

		if (targetWindowState != nullptr && targetWindowState->displacementComparison.has_value())
		{
			RendererWindowState::DisplacementComparisonState &state = *targetWindowState->displacementComparison;
			ImGui::Separator();

			if (!state.result.latticeMismatchWarning.empty())
				ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.2f, 1.0f), "%s", state.result.latticeMismatchWarning.c_str());

			ImGui::Checkbox("Show arrows", &state.visible);
			ImGui::SameLine();
			ImGui::Checkbox("Only for visible atoms", &state.onlyForVisibleAtoms);

			float maxMagnitude = 0.0f;
			for (const AtomDisplacement &displacement : state.result.matches)
				maxMagnitude = std::max(maxMagnitude, displacement.magnitudeAngstrom);
			ImGui::SliderFloat("Threshold (A)", &state.displayThresholdAngstrom, 0.0f, std::max(maxMagnitude, 0.001f));
			if (ImGui::IsItemDeactivatedAfterEdit())
				publishPersistedStateChanged();

			ImGui::Text(
				"%zu matched, %zu vacancy-like, %zu interstitial-like",
				state.result.matches.size(),
				state.result.unmatchedReferenceAtomIndices.size(),
				state.result.unmatchedComparisonAtoms.size());

			if (ImGui::TreeNode("Arrow settings"))
			{
				ImGui::ColorEdit3("Low magnitude", &state.lowMagnitudeColor.x);
				ImGui::ColorEdit3("High magnitude", &state.highMagnitudeColor.x);
				ImGui::Checkbox("Normalize to visible max", &state.normalizeColorToVisibleMax);
				if (!state.normalizeColorToVisibleMax)
					ImGui::SliderFloat("Color ceiling (A)", &state.fixedNormalizationMaxAngstrom, 0.001f, std::max(maxMagnitude, 0.1f));
				ImGui::TreePop();
			}
		}

		ImGui::End();
	}
} // namespace DefectStudio
