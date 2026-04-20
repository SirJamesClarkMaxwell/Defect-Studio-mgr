#include "Core/dspch.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <unordered_map>

#include <imgui.h>

#include "App/Application.hpp"
#include "Presentation/Panels/ProgressMonitorWindow.hpp"

namespace DefectStudio
{

	struct DisplayMetrics
	{
		float progressPercent = 0.0f;
		JobStatus status = JobStatus::Queued;
		bool aggregated = false;
	};

	using SnapshotRef = Ref<ProgressEntrySnapshot>;
	using SnapshotWeak = WeakRef<ProgressEntrySnapshot>;
	using ChildrenByParent = std::unordered_map<JobId, std::vector<SnapshotWeak>>;

	constexpr bool isFinishedStatusLocal(JobStatus status)
	{
		return status == JobStatus::Completed || status == JobStatus::Failed || status == JobStatus::Cancelled;
	}

	constexpr bool isActiveStatusLocal(JobStatus status)
	{
		return status == JobStatus::Queued || status == JobStatus::Running;
	}

	std::vector<SnapshotRef> makeSnapshotRefs(const std::vector<ProgressEntrySnapshot> &allJobs)
	{
		std::vector<SnapshotRef> refs;
		refs.reserve(allJobs.size());
		for (const auto &snapshot : allJobs)
			refs.push_back(CreateRef<ProgressEntrySnapshot>(snapshot));
		return refs;
	}

	bool compareSnapshotWeakByIdDesc(const SnapshotWeak &left, const SnapshotWeak &right)
	{
		const auto leftRef = left.lock();
		const auto rightRef = right.lock();
		if (!leftRef)
			return false;
		if (!rightRef)
			return true;
		return leftRef->id > rightRef->id;
	}

	SnapshotWeak findSnapshotById(const std::vector<SnapshotRef> &allJobs, JobId id)
	{
		const auto it = std::find_if(allJobs.begin(), allJobs.end(), [id](const SnapshotRef &snapshot) {
			return snapshot && snapshot->id == id;
		});

		return it == allJobs.end() ? SnapshotWeak{} : CreateWeakRef(*it);
	}

	ChildrenByParent buildChildrenByParent(const std::vector<SnapshotRef> &allJobs)
	{
		ChildrenByParent childrenByParent;
		for (const auto &snapshot : allJobs)
		{
			if (!snapshot)
				continue;
			childrenByParent[snapshot->parentId].push_back(CreateWeakRef(snapshot));
		}

		for (auto &entry : childrenByParent)
			std::sort(entry.second.begin(), entry.second.end(), compareSnapshotWeakByIdDesc);

		return childrenByParent;
	}

	template <typename Predicate>
	int countMatchingDescendants(
		const ChildrenByParent &childrenByParent,
		JobId parentId,
		Predicate predicate)
	{
		const auto childrenIt = childrenByParent.find(parentId);
		if (childrenIt == childrenByParent.end())
			return 0;

		int count = 0;
		for (const auto &childWeak : childrenIt->second)
		{
			const auto child = childWeak.lock();
			if (!child)
				continue;

			if (predicate(*child))
				++count;
			count += countMatchingDescendants(childrenByParent, child->id, predicate);
		}
		return count;
	}

	int countAllDescendantsLocal(
		const ChildrenByParent &childrenByParent,
		JobId parentId)
	{
		return countMatchingDescendants(childrenByParent, parentId, [](const ProgressEntrySnapshot &) {
			return true;
		});
	}

	int countFinishedDescendantsLocal(
		const ChildrenByParent &childrenByParent,
		JobId parentId)
	{
		return countMatchingDescendants(childrenByParent, parentId, [](const ProgressEntrySnapshot &entry) {
			return isFinishedStatusLocal(entry.status);
		});
	}

	DisplayMetrics buildDisplayMetrics(
		const ProgressEntrySnapshot &snapshot,
		const ChildrenByParent &childrenByParent)
	{
		DisplayMetrics metrics;
		metrics.status = snapshot.status;

		const int totalDescendants = countAllDescendantsLocal(childrenByParent, snapshot.id);
		if (totalDescendants == 0)
		{
			metrics.progressPercent = snapshot.totalWork <= 0.0f
				? 0.0f
				: std::clamp((snapshot.completedWork / snapshot.totalWork) * 100.0f, 0.0f, 100.0f);
			return metrics;
		}

		metrics.aggregated = true;
		const int finishedDescendants = countFinishedDescendantsLocal(childrenByParent, snapshot.id);
		metrics.progressPercent = totalDescendants <= 0 ? 0.0f : std::clamp((static_cast<float>(finishedDescendants) / static_cast<float>(totalDescendants)) * 100.0f, 0.0f, 100.0f);

		const bool hasFailedDescendant = countMatchingDescendants(childrenByParent, snapshot.id, [](const ProgressEntrySnapshot &entry) {
			return entry.status == JobStatus::Failed;
		}) > 0;
		const bool hasActiveDescendant = countMatchingDescendants(childrenByParent, snapshot.id, [](const ProgressEntrySnapshot &entry) {
			return entry.status == JobStatus::Queued || entry.status == JobStatus::Running;
		}) > 0;
		const bool hasCancelledDescendant = countMatchingDescendants(childrenByParent, snapshot.id, [](const ProgressEntrySnapshot &entry) {
			return entry.status == JobStatus::Cancelled;
		}) > 0;

		if (hasFailedDescendant || snapshot.status == JobStatus::Failed)
			metrics.status = JobStatus::Failed;
		else if (hasActiveDescendant || snapshot.status == JobStatus::Queued || snapshot.status == JobStatus::Running)
			metrics.status = JobStatus::Running;
		else if (hasCancelledDescendant || snapshot.status == JobStatus::Cancelled)
			metrics.status = JobStatus::Cancelled;
		else
			metrics.status = JobStatus::Completed;

		return metrics;
	}

	void collectSubtreeIds(
		const ChildrenByParent &childrenByParent,
		JobId rootId,
		std::vector<JobId> &ids)
	{
		ids.push_back(rootId);
		const auto childrenIt = childrenByParent.find(rootId);
		if (childrenIt == childrenByParent.end())
			return;

		for (const auto &childWeak : childrenIt->second)
		{
			const auto child = childWeak.lock();
			if (!child)
				continue;
			collectSubtreeIds(childrenByParent, child->id, ids);
		}
	}

	void collectVisibleRows(
		const ChildrenByParent &childrenByParent,
		const std::unordered_set<JobId> &expandedJobIds,
		JobId parentId,
		int depth,
		std::vector<VisibleJobRow> &rows)
	{
		const auto childrenIt = childrenByParent.find(parentId);
		if (childrenIt == childrenByParent.end())
			return;

		for (const auto &snapshotWeak : childrenIt->second)
		{
			const auto snapshot = snapshotWeak.lock();
			if (!snapshot)
				continue;

			const bool hasChildren = childrenByParent.find(snapshot->id) != childrenByParent.end();
			rows.push_back(VisibleJobRow{snapshotWeak, depth, hasChildren});
			if (hasChildren && expandedJobIds.find(snapshot->id) != expandedJobIds.end())
				collectVisibleRows(childrenByParent, expandedJobIds, snapshot->id, depth + 1, rows);
		}
	}

	ProgressMonitorWindow::ProgressMonitorWindow(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
	}

	Ref<IPanel> ProgressMonitorWindow::Clone() const
	{
		return CreateRef<ProgressMonitorWindow>(*this);
	}

	void ProgressMonitorWindow::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(1120.0f, 700.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		auto &progressTracker = Application::Get().GetProgressTracker();
		auto snapshots = progressTracker.GetAllSnapshots();
		std::vector<VisibleJobRow> visibleRows;
		buildVisibleRows(snapshots, visibleRows);
		updateFocusedSelectionFallback();
		ensureSelectionVisible(snapshots);

		ImGui::Text("Jobs: %zu", visibleRows.size());
		ImGui::SameLine();
		ImGui::TextDisabled("Use Ctrl for multi-select and Shift for range selection.");
		ImGui::SameLine();
		ImGui::TextDisabled("Selected: %zu", m_SelectedJobIds.size());

		auto &jobSystem = Application::Get().GetJobSystem();
		const int finishedSelected = countFinishedSelected(snapshots);
		if (finishedSelected == 0)
			ImGui::BeginDisabled();
		if (ImGui::Button("Delete selected finished") && finishedSelected > 0)
			requestDeleteSelectionConfirmation(snapshots);
		if (finishedSelected == 0)
			ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel selected") && !m_SelectedJobIds.empty())
		{
			for (const JobId jobId : m_SelectedJobIds)
				(void)jobSystem.RequestCancel(jobId);
		}
		ImGui::SameLine();
		if (ImGui::Button("Resume selected") && !m_SelectedJobIds.empty())
		{
			for (const JobId jobId : m_SelectedJobIds)
				(void)jobSystem.Resume(jobId);
		}

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_SelectedJobIds.empty())
			requestDeleteSelectionConfirmation(snapshots);
		renderDeleteConfirmationPopup();

		ImGui::Separator();

		const float splitterHeight = 10.0f;
		const float minListHeight = 120.0f;
		const float minDetailsHeight = 80.0f;
		const float totalAvailHeight = ImGui::GetContentRegionAvail().y;
		const float maxDetailsHeight = std::max(minDetailsHeight, totalAvailHeight - minListHeight - splitterHeight);
		m_DetailsHeight = std::clamp(m_DetailsHeight, minDetailsHeight, maxDetailsHeight);
		const float listHeight = std::max(minListHeight, totalAvailHeight - m_DetailsHeight - splitterHeight);

		ImGui::BeginChild("JobsListRegion", ImVec2(0.0f, listHeight), true);
		renderJobList(visibleRows, snapshots);
		ImGui::EndChild();

		const ImVec2 splitterSize(ImGui::GetContentRegionAvail().x, splitterHeight);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.34f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.42f, 1.0f));
		ImGui::Button("##JobsSplitter", splitterSize);
		ImGui::PopStyleColor(3);
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (ImGui::IsItemActive())
			m_DetailsHeight = std::clamp(m_DetailsHeight - ImGui::GetIO().MouseDelta.y, minDetailsHeight, maxDetailsHeight);

		ImGui::BeginChild("JobDetailsRegion", ImVec2(0.0f, m_DetailsHeight), true);
		renderSelectedJobDetails(snapshots);
		ImGui::EndChild();


		ImGui::End();
	}

	void ProgressMonitorWindow::buildVisibleRows(const std::vector<ProgressEntrySnapshot> &allJobs, std::vector<VisibleJobRow> &rows) const
	{
		m_RowSnapshotRefs = makeSnapshotRefs(allJobs);
		const auto childrenByParent = buildChildrenByParent(m_RowSnapshotRefs);
		collectVisibleRows(childrenByParent, m_ExpandedJobIds, 0, 0, rows);
	}

	void ProgressMonitorWindow::ensureSelectionVisible(const std::vector<ProgressEntrySnapshot> &allJobs)
	{
		if (m_FocusedJobId == 0)
			return;

		std::unordered_map<JobId, JobId> parentById;
		parentById.reserve(allJobs.size());
		for (const auto &job : allJobs)
			parentById.emplace(job.id, job.parentId);

		JobId current = m_FocusedJobId;
		while (current != 0)
		{
			const auto parentIt = parentById.find(current);
			if (parentIt == parentById.end())
				break;

			current = parentIt->second;
			if (current != 0)
				m_ExpandedJobIds.insert(current);
		}
	}

	void ProgressMonitorWindow::renderJobList(const std::vector<VisibleJobRow> &rows, const std::vector<ProgressEntrySnapshot> &allJobs)
	{
		const auto childrenByParent = buildChildrenByParent(m_RowSnapshotRefs);

		constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg
			| ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable
			| ImGuiTableFlags_Reorderable
			| ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_SizingStretchProp
			| ImGuiTableFlags_BordersOuter;

		if (!ImGui::BeginTable("ProgressJobsTable", 6, tableFlags, ImVec2(0.0f, 0.0f)))
			return;

		ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch, 0.44f);
		ImGui::TableSetupColumn("Subtasks", ImGuiTableColumnFlags_WidthFixed, 84.0f);
		ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 84.0f);
		ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("ETA", ImGuiTableColumnFlags_WidthFixed, 96.0f);
		ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 74.0f);
		ImGui::TableHeadersRow();

		for (int rowIndex = 0; rowIndex < static_cast<int>(rows.size()); ++rowIndex)
		{
			const auto &row = rows[static_cast<std::size_t>(rowIndex)];
			const auto snapshotRef = row.snapshot.lock();
			if (!snapshotRef)
				continue;

			const auto &snapshot = *snapshotRef;
			const DisplayMetrics metrics = buildDisplayMetrics(snapshot, childrenByParent);

			ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(static_cast<int>(snapshot.id));

			const bool selected = isSelected(snapshot.id);
			const std::string displayLabel = buildDisplayLabel(snapshot);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + static_cast<float>(row.depth) * 18.0f);
			ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (!row.hasChildren)
				treeFlags |= ImGuiTreeNodeFlags_Leaf;
			if (selected)
				treeFlags |= ImGuiTreeNodeFlags_Selected;

			ImGui::SetNextItemOpen(m_ExpandedJobIds.find(snapshot.id) != m_ExpandedJobIds.end(), ImGuiCond_Always);
			const bool nodeOpen = ImGui::TreeNodeEx(displayLabel.c_str(), treeFlags);
			if (ImGui::IsItemClicked())
				applySelection(rows, rowIndex, snapshot.id);
			if (row.hasChildren && ImGui::IsItemToggledOpen())
			{
				if (nodeOpen)
					m_ExpandedJobIds.insert(snapshot.id);
				else
					m_ExpandedJobIds.erase(snapshot.id);
			}

			if (ImGui::BeginPopupContextItem())
			{
				renderJobContextMenu(snapshot);
				ImGui::EndPopup();
			}

			ImGui::PopID();

			ImGui::TableSetColumnIndex(1);
			const int allSubtasks = countAllDescendants(allJobs, snapshot.id);
			const int finishedSubtasks = countFinishedDescendants(allJobs, snapshot.id);
			if (allSubtasks == 0)
				ImGui::TextUnformatted("-");
			else
				ImGui::Text("%d/%d", finishedSubtasks, allSubtasks);

			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("%d%%", static_cast<int>(metrics.progressPercent));

			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(toPriorityLabel(snapshot.priority));

			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(formatEstimatedTime(snapshot).c_str());

			ImGui::TableSetColumnIndex(5);
			const ImVec4 color = toStatusColor(metrics.status);
			ImGui::TextColored(color, "%s", toStatusBadge(metrics.status));
		}

		ImGui::EndTable();
	}

	void ProgressMonitorWindow::renderSelectedJobDetails(const std::vector<ProgressEntrySnapshot> &allJobs)
	{
		const auto snapshotRefs = makeSnapshotRefs(allJobs);
		const auto childrenByParent = buildChildrenByParent(snapshotRefs);

		if (m_FocusedJobId == 0)
		{
			ImGui::TextUnformatted("No job selected.");
			return;
		}

		const auto snapshotRef = findSnapshotById(snapshotRefs, m_FocusedJobId).lock();
		if (!snapshotRef)
		{
			ImGui::TextUnformatted("Selected job is no longer available.");
			return;
		}

		const auto &snapshot = *snapshotRef;
		const DisplayMetrics metrics = buildDisplayMetrics(snapshot, childrenByParent);
		std::unordered_map<JobId, JobId> parentById;
		parentById.reserve(allJobs.size());
		for (const auto &job : allJobs)
			parentById.emplace(job.id, job.parentId);

		std::vector<SnapshotWeak> ancestry;
		for (JobId current = snapshot.parentId; current != 0;)
		{
			const auto parentIt = parentById.find(current);
			if (parentIt == parentById.end())
				break;

			const auto parentSnapshot = findSnapshotById(snapshotRefs, current);
			if (parentSnapshot.expired())
				break;

			ancestry.push_back(parentSnapshot);
			current = parentIt->second;
		}
		std::vector<SnapshotWeak> internalJobs;
		for (const auto &candidateRef : snapshotRefs)
		{
			if (!candidateRef)
				continue;
			const auto &candidate = *candidateRef;

			if (candidate.id == snapshot.id || candidate.parentId == 0)
				continue;

			JobId currentParent = candidate.parentId;
			while (currentParent != 0)
			{
				if (currentParent == snapshot.id)
				{
					internalJobs.push_back(CreateWeakRef(candidateRef));
					break;
				}

				const auto parentIt = parentById.find(currentParent);
				if (parentIt == parentById.end())
					break;

				currentParent = parentIt->second;
			}
		}

		std::sort(internalJobs.begin(), internalJobs.end(), compareSnapshotWeakByIdDesc);

		const bool hasInternalJobs = !internalJobs.empty();
		const float splitterHeight = hasInternalJobs ? 10.0f : 0.0f;
		const float minTopHeight = 120.0f;
		const float minBottomHeight = 120.0f;
		const float totalAvailHeight = ImGui::GetContentRegionAvail().y;
		const float maxBottomHeight = std::max(minBottomHeight, totalAvailHeight - minTopHeight - splitterHeight);
		if (hasInternalJobs)
			m_InternalTasksHeight = std::clamp(m_InternalTasksHeight, minBottomHeight, maxBottomHeight);
		const float topHeight = hasInternalJobs
			? std::max(minTopHeight, totalAvailHeight - m_InternalTasksHeight - splitterHeight)
			: std::max(0.0f, totalAvailHeight);

		ImGui::BeginChild("SelectedJobSummaryRegion", ImVec2(0.0f, topHeight), false);
		ImGui::Text("Selected: #%llu | %s", static_cast<unsigned long long>(snapshot.id), snapshot.label.c_str());
		ImGui::SameLine();
		ImGui::TextColored(toStatusColor(metrics.status), "%s", toStatusBadge(metrics.status));
		if (metrics.status != snapshot.status)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("raw: %s", toStatusBadge(snapshot.status));
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Priority: %s", toPriorityLabel(snapshot.priority));
		if (snapshot.parentId != 0)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Jump to parent"))
			{
				m_SelectedJobIds.assign(1, snapshot.parentId);
				m_FocusedJobId = snapshot.parentId;
				m_SelectionAnchorIndex = -1;
			}
		}
		ImGui::Separator();

		if (ImGui::BeginTable("SelectedJobDetails", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			const auto addRow = [](const char *label, const std::string &value) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(label);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextWrapped("%s", value.c_str());
			};

			addRow("Type", snapshot.source);
			addRow("Stage", snapshot.currentStage.empty() ? std::string("-") : snapshot.currentStage);
			addRow("Message", snapshot.currentMessage.empty() ? std::string("-") : snapshot.currentMessage);
			addRow("Parent", snapshot.parentId == 0 ? std::string("-") : std::to_string(snapshot.parentId));
			addRow("Priority", toPriorityLabel(snapshot.priority));
			addRow("Status", toStatusLabel(snapshot.status));
			if (metrics.aggregated)
			{
				addRow("Effective Status", toStatusLabel(metrics.status));
				addRow("Effective Progress", std::to_string(static_cast<int>(metrics.progressPercent)) + "%");
			}
			addRow("ETA", formatEstimatedTime(snapshot));
			addRow("Progress", std::to_string(static_cast<int>(snapshot.completedWork)) + " / " + std::to_string(static_cast<int>(snapshot.totalWork)));
			addRow("Created", formatTimePoint(snapshot.createdAt));
			addRow("Started", formatTimePoint(snapshot.startedAt));
			addRow("Finished", formatTimePoint(snapshot.finishedAt));
			addRow("Duration", formatDuration(snapshot.startedAt, snapshot.finishedAt));

			ImGui::EndTable();
		}

		if (!snapshot.errorSummary.empty())
			ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Error: %s", snapshot.errorSummary.c_str());

		std::reverse(ancestry.begin(), ancestry.end());
		if (!ancestry.empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Path:");
			ImGui::SameLine();
			for (std::size_t index = 0; index < ancestry.size(); ++index)
			{
				const auto node = ancestry[index].lock();
				if (!node)
					continue;

				ImGui::PushID(static_cast<int>(node->id));
				if (ImGui::SmallButton(node->label.c_str()))
				{
					m_SelectedJobIds.assign(1, node->id);
					m_FocusedJobId = node->id;
					m_SelectionAnchorIndex = -1;
				}
				ImGui::PopID();
				if (index + 1 < ancestry.size())
				{
					ImGui::SameLine();
					ImGui::TextDisabled(">");
					ImGui::SameLine();
				}
			}
		}

		if (!hasInternalJobs)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("No internal tasks for this job.");
		}
		ImGui::EndChild();

		if (!hasInternalJobs)
		{
			return;
		}

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.34f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.42f, 1.0f));
		ImGui::Button("##InternalTasksSplitter", ImVec2(ImGui::GetContentRegionAvail().x, splitterHeight));
		ImGui::PopStyleColor(3);
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (ImGui::IsItemActive())
			m_InternalTasksHeight = std::clamp(m_InternalTasksHeight - ImGui::GetIO().MouseDelta.y, minBottomHeight, maxBottomHeight);

		ImGui::BeginChild("InternalTasksRegion", ImVec2(0.0f, m_InternalTasksHeight), false);
		ImGui::TextUnformatted("Internal Tasks");
		constexpr ImGuiTableFlags internalFlags = ImGuiTableFlags_RowBg
			| ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_BordersOuter
			| ImGuiTableFlags_Resizable
			| ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_SizingStretchProp;

		const float internalTableHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing());
		if (ImGui::BeginTable("InternalTasksTable", 7, internalFlags, ImVec2(0.0f, internalTableHeight)))
		{
			ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.40f);
			ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 82.0f);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 76.0f);
			ImGui::TableSetupColumn("ETA", ImGuiTableColumnFlags_WidthFixed, 92.0f);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 96.0f);
			ImGui::TableHeadersRow();

			for (const auto &internalWeak : internalJobs)
			{
				const auto internal = internalWeak.lock();
				if (!internal)
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%llu", static_cast<unsigned long long>(internal->id));
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(internal->label.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%d%%", static_cast<int>(std::clamp(
					internal->totalWork <= 0.0f ? 0.0f : (internal->completedWork / internal->totalWork) * 100.0f,
					0.0f,
					100.0f)));
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(toPriorityLabel(internal->priority));
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(formatEstimatedTime(*internal).c_str());
				ImGui::TableSetColumnIndex(5);
				ImGui::TextColored(toStatusColor(internal->status), "%s", toStatusBadge(internal->status));
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(formatDuration(internal->startedAt, internal->finishedAt).c_str());
			}

			ImGui::EndTable();
		}
		ImGui::EndChild();
	}

	void ProgressMonitorWindow::applySelection(const std::vector<VisibleJobRow> &rows, int rowIndex, JobId jobId)
	{
		const ImGuiIO &io = ImGui::GetIO();
		const bool ctrl = io.KeyCtrl;
		const bool shift = io.KeyShift;

		if (shift && m_SelectionAnchorIndex >= 0 && m_SelectionAnchorIndex < static_cast<int>(rows.size()))
		{
			m_SelectedJobIds.clear();
			const int beginIndex = std::min(m_SelectionAnchorIndex, rowIndex);
			const int endIndex = std::max(m_SelectionAnchorIndex, rowIndex);
			for (int index = beginIndex; index <= endIndex; ++index)
			{
				const auto snapshot = rows[static_cast<std::size_t>(index)].snapshot.lock();
				if (snapshot)
					m_SelectedJobIds.push_back(snapshot->id);
			}
			m_FocusedJobId = jobId;
			return;
		}

		if (!ctrl && !shift && m_SelectedJobIds.size() == 1 && m_SelectedJobIds.front() == jobId)
		{
			m_SelectedJobIds.clear();
			m_FocusedJobId = 0;
			m_SelectionAnchorIndex = -1;
			return;
		}

		if (ctrl)
		{
			const auto existing = std::find(m_SelectedJobIds.begin(), m_SelectedJobIds.end(), jobId);
			if (existing != m_SelectedJobIds.end())
				m_SelectedJobIds.erase(existing);
			else
				m_SelectedJobIds.push_back(jobId);
			m_SelectionAnchorIndex = rowIndex;
			m_FocusedJobId = jobId;
			return;
		}

		m_SelectedJobIds.clear();
		m_SelectedJobIds.push_back(jobId);
		m_SelectionAnchorIndex = rowIndex;
		m_FocusedJobId = jobId;
	}

	void ProgressMonitorWindow::updateFocusedSelectionFallback()
	{
		if (m_SelectedJobIds.empty())
			m_FocusedJobId = 0;
		else if (std::find(m_SelectedJobIds.begin(), m_SelectedJobIds.end(), m_FocusedJobId) == m_SelectedJobIds.end())
			m_FocusedJobId = m_SelectedJobIds.front();
	}

	bool ProgressMonitorWindow::isSelected(JobId jobId) const
	{
		return std::find(m_SelectedJobIds.begin(), m_SelectedJobIds.end(), jobId) != m_SelectedJobIds.end();
	}

	void ProgressMonitorWindow::renderJobContextMenu(const ProgressEntrySnapshot &snapshot)
	{
		auto &jobSystem = Application::Get().GetJobSystem();
		auto &progressTracker = Application::Get().GetProgressTracker();
		const JobStatus status = snapshot.status;
		const bool isCancelled = status == JobStatus::Cancelled;
		const bool isCompleted = status == JobStatus::Completed;
		const bool isFailed = status == JobStatus::Failed;
		const bool isFinished = isCompleted || isFailed || isCancelled;
		const bool canCancel = isActiveStatusLocal(status);
		const bool canResume = isCancelled || isActiveStatusLocal(status);
		const bool canReset = isFinished;
		const bool canRetry = isFinished;
		const bool canDeleteFromHistory = isFinished;

		if (ImGui::MenuItem("Copy JobId"))
			ImGui::SetClipboardText(std::to_string(snapshot.id).c_str());
		if (ImGui::MenuItem("Copy Label"))
			ImGui::SetClipboardText(snapshot.label.c_str());
		if (ImGui::MenuItem("Copy Source"))
			ImGui::SetClipboardText(snapshot.source.c_str());

		ImGui::Separator();

		if (ImGui::MenuItem("Cancel", nullptr, false, canCancel))
			(void)jobSystem.RequestCancel(snapshot.id);
		if (ImGui::MenuItem("Resume", nullptr, false, canResume))
			(void)jobSystem.Resume(snapshot.id);
		if (ImGui::MenuItem("Reset", nullptr, false, canReset))
			(void)jobSystem.Reset(snapshot.id);
		if (ImGui::MenuItem("Retry", nullptr, false, canRetry))
			(void)jobSystem.Retry(snapshot.id);

		ImGui::Separator();
		if (ImGui::MenuItem("Delete from history", nullptr, false, canDeleteFromHistory))
		{
			if (jobSystem.RemoveFromHistory(snapshot.id))
				(void)progressTracker.RemoveEntry(snapshot.id);
		}

		if (m_SelectedJobIds.size() > 1 && isSelected(snapshot.id))
		{
			ImGui::Separator();
			if (ImGui::MenuItem("Delete selected finished"))
				requestDeleteSelectionConfirmation(Application::Get().GetProgressTracker().GetAllSnapshots());
		}
	}

	void ProgressMonitorWindow::renderDeleteConfirmationPopup()
	{
		if (m_OpenDeleteConfirmPopup)
		{
			ImGui::OpenPopup("ConfirmDeleteJobsPopup");
			m_OpenDeleteConfirmPopup = false;
		}

		if (!ImGui::BeginPopupModal("ConfirmDeleteJobsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (m_DeletePendingIds.empty())
		{
			ImGui::TextUnformatted("No finished jobs selected.");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::Text("Delete %zu selected finished job(s) from history?", m_DeletePendingIds.size());
		if (m_DeletePendingSkipped > 0)
			ImGui::TextDisabled("%d selected job(s) are still active and will be skipped.", m_DeletePendingSkipped);

		const bool confirmByEnter = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));

		if (ImGui::Button("Delete") || confirmByEnter)
		{
			removeFromHistory(m_DeletePendingIds);
			m_DeletePendingIds.clear();
			m_DeletePendingSkipped = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_DeletePendingIds.clear();
			m_DeletePendingSkipped = 0;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void ProgressMonitorWindow::requestDeleteSelectionConfirmation(const std::vector<ProgressEntrySnapshot> &allJobs)
	{
		const auto snapshotRefs = makeSnapshotRefs(allJobs);
		const auto childrenByParent = buildChildrenByParent(snapshotRefs);
		m_DeletePendingIds.clear();
		m_DeletePendingSkipped = 0;

		for (const JobId selectedId : m_SelectedJobIds)
		{
			const auto snapshot = findSnapshotById(snapshotRefs, selectedId).lock();
			if (!snapshot)
				continue;

			const DisplayMetrics metrics = buildDisplayMetrics(*snapshot, childrenByParent);
			if (isFinishedStatusLocal(metrics.status))
				m_DeletePendingIds.push_back(selectedId);
			else
				++m_DeletePendingSkipped;
		}

		m_OpenDeleteConfirmPopup = true;
	}

	void ProgressMonitorWindow::removeFromHistory(const std::vector<JobId> &ids)
	{
		auto &jobSystem = Application::Get().GetJobSystem();
		auto &progressTracker = Application::Get().GetProgressTracker();
		const auto snapshots = progressTracker.GetAllSnapshots();
		const auto snapshotRefs = makeSnapshotRefs(snapshots);
		const auto childrenByParent = buildChildrenByParent(snapshotRefs);
		std::vector<JobId> idsToRemove;

		for (const JobId id : ids)
		{
			const auto snapshot = findSnapshotById(snapshotRefs, id).lock();
			if (!snapshot)
				continue;

			const DisplayMetrics metrics = buildDisplayMetrics(*snapshot, childrenByParent);
			if (!isFinishedStatusLocal(metrics.status))
				continue;

			collectSubtreeIds(childrenByParent, id, idsToRemove);
		}

		std::sort(idsToRemove.begin(), idsToRemove.end());
		idsToRemove.erase(std::unique(idsToRemove.begin(), idsToRemove.end()), idsToRemove.end());

		for (const JobId id : idsToRemove)
		{
			if (jobSystem.RemoveFromHistory(id))
				(void)progressTracker.RemoveEntry(id);
		}

		m_SelectedJobIds.erase(std::remove_if(m_SelectedJobIds.begin(), m_SelectedJobIds.end(), [&idsToRemove](JobId id) {
			return std::find(idsToRemove.begin(), idsToRemove.end(), id) != idsToRemove.end();
		}), m_SelectedJobIds.end());
		updateFocusedSelectionFallback();
	}

	void ProgressMonitorWindow::removeSelectedFromHistory()
	{
		const auto snapshots = Application::Get().GetProgressTracker().GetAllSnapshots();
		requestDeleteSelectionConfirmation(snapshots);
	}

	int ProgressMonitorWindow::countFinishedSelected(const std::vector<ProgressEntrySnapshot> &allJobs) const
	{
		const auto snapshotRefs = makeSnapshotRefs(allJobs);
		const auto childrenByParent = buildChildrenByParent(snapshotRefs);
		int count = 0;
		for (const JobId id : m_SelectedJobIds)
		{
			const auto snapshot = findSnapshotById(snapshotRefs, id).lock();
			if (snapshot)
			{
				const DisplayMetrics metrics = buildDisplayMetrics(*snapshot, childrenByParent);
				if (isFinishedStatusLocal(metrics.status))
					++count;
			}
		}
		return count;
	}

	int ProgressMonitorWindow::countFinishedDescendants(const std::vector<ProgressEntrySnapshot> &allJobs, JobId parentId)
	{
		int count = 0;
		for (const auto &snapshot : allJobs)
		{
			if (snapshot.parentId != parentId)
				continue;

			if (isFinishedStatusLocal(snapshot.status))
				++count;
			count += countFinishedDescendants(allJobs, snapshot.id);
		}
		return count;
	}

	int ProgressMonitorWindow::countAllDescendants(const std::vector<ProgressEntrySnapshot> &allJobs, JobId parentId)
	{
		int count = 0;
		for (const auto &snapshot : allJobs)
		{
			if (snapshot.parentId != parentId)
				continue;

			++count;
			count += countAllDescendants(allJobs, snapshot.id);
		}
		return count;
	}

	const char *ProgressMonitorWindow::toStatusLabel(JobStatus status)
	{
		switch (status)
		{
		case JobStatus::Queued:
			return "Queued";
		case JobStatus::Running:
			return "Running";
		case JobStatus::Completed:
			return "Completed";
		case JobStatus::Failed:
			return "Failed";
		case JobStatus::Cancelled:
			return "Cancelled";
		default:
			return "Unknown";
		}
	}

	const char *ProgressMonitorWindow::toStatusBadge(JobStatus status)
	{
		switch (status)
		{
		case JobStatus::Queued:
			return "WAIT";
		case JobStatus::Running:
			return "RUN";
		case JobStatus::Completed:
			return "OK";
		case JobStatus::Failed:
			return "ERR";
		case JobStatus::Cancelled:
			return "STOP";
		default:
			return "?";
		}
	}

	ImVec4 ProgressMonitorWindow::toStatusColor(JobStatus status)
	{
		switch (status)
		{
		case JobStatus::Queued:
			return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
		case JobStatus::Running:
			return ImVec4(0.20f, 0.60f, 1.00f, 1.0f);
		case JobStatus::Completed:
			return ImVec4(0.20f, 0.75f, 0.35f, 1.0f);
		case JobStatus::Failed:
			return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
		case JobStatus::Cancelled:
			return ImVec4(0.95f, 0.70f, 0.20f, 1.0f);
		default:
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	const char *ProgressMonitorWindow::toPriorityLabel(JobPriority priority)
	{
		switch (priority)
		{
		case JobPriority::Lowest:
			return "Lowest";
		case JobPriority::Low:
			return "Low";
		case JobPriority::Normal:
			return "Normal";
		case JobPriority::High:
			return "High";
		case JobPriority::Highest:
			return "Highest";
		default:
			return "Normal";
		}
	}

	std::string ProgressMonitorWindow::formatDurationHms(std::chrono::milliseconds duration)
	{
		if (duration.count() < 0)
			duration = std::chrono::milliseconds(0);

		const auto secondsTotal = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
		const auto hours = secondsTotal / 3600;
		const auto minutes = (secondsTotal % 3600) / 60;
		const auto seconds = secondsTotal % 60;

		char buffer[16] = {};
		std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld", static_cast<long long>(hours), static_cast<long long>(minutes), static_cast<long long>(seconds));
		return buffer;
	}

	std::string ProgressMonitorWindow::formatEstimatedTime(const ProgressEntrySnapshot &snapshot)
	{
		if (snapshot.status == JobStatus::Completed || snapshot.status == JobStatus::Failed || snapshot.status == JobStatus::Cancelled)
			return "-";

		if (snapshot.status == JobStatus::Queued)
			return "queued";

		if (snapshot.startedAt == Time::TimePoint{} || snapshot.totalWork <= 0.0f || snapshot.completedWork <= 0.0f)
			return "-";

		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Time::Now() - snapshot.startedAt);
		if (elapsed.count() <= 0)
			return "-";

		const float speed = snapshot.completedWork / static_cast<float>(elapsed.count());
		if (speed <= 0.000001f)
			return "-";

		const float remainingWork = std::max(0.0f, snapshot.totalWork - snapshot.completedWork);
		const auto remainingMs = static_cast<long long>(remainingWork / speed);
		return formatDurationHms(std::chrono::milliseconds(remainingMs));
	}

	std::string ProgressMonitorWindow::formatTimePoint(const Time::TimePoint &timePoint)
	{
		if (timePoint == Time::TimePoint{})
			return "-";

		const auto t = std::chrono::system_clock::to_time_t(timePoint);
		std::tm localTm{};
#ifdef _WIN32
		localtime_s(&localTm, &t);
#else
		localtime_r(&t, &localTm);
#endif
		char buffer[64] = {};
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTm);
		return buffer;
	}

	std::string ProgressMonitorWindow::formatDuration(const Time::TimePoint &startedAt, const Time::TimePoint &finishedAt)
	{
		if (startedAt == Time::TimePoint{})
			return "-";

		const auto effectiveEnd = finishedAt == Time::TimePoint{} ? Time::Now() : finishedAt;
		if (effectiveEnd < startedAt)
			return "-";

		return formatDurationHms(std::chrono::duration_cast<std::chrono::milliseconds>(effectiveEnd - startedAt));
	}

	std::string ProgressMonitorWindow::buildDisplayLabel(const ProgressEntrySnapshot &snapshot)
	{
		std::ostringstream stream;
		stream << "#" << snapshot.id << " | " << snapshot.label;
		if (snapshot.parentId != 0)
			stream << "  [child]";
		return stream.str();
	}
} // namespace DefectStudio
