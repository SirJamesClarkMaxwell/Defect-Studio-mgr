#pragma once

#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Presentation/Panels/IPanel.hpp"

struct ImVec4;

namespace DefectStudio
{
	struct VisibleJobRow
	{
		const ProgressEntrySnapshot *snapshot = nullptr;
		int depth = 0;
		bool hasChildren = false;
	};
	class ProgressMonitorWindow final : public IPanel
	{
	public:
		explicit ProgressMonitorWindow(std::string title = "Progress Monitor", bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:

		std::vector<JobId> m_SelectedJobIds;
		JobId m_FocusedJobId = 0;
		int m_SelectionAnchorIndex = -1;
		float m_DetailsHeight = 220.0f;
		std::unordered_set<JobId> m_ExpandedJobIds;
		std::vector<JobId> m_DeletePendingIds;
		int m_DeletePendingSkipped = 0;
		bool m_OpenDeleteConfirmPopup = false;

		void buildVisibleRows(const std::vector<ProgressEntrySnapshot> &allJobs, std::vector<VisibleJobRow> &rows) const;
		void ensureSelectionVisible(const std::vector<ProgressEntrySnapshot> &allJobs);
		void renderJobList(const std::vector<VisibleJobRow> &rows, const std::vector<ProgressEntrySnapshot> &allJobs);
		void renderSelectedJobDetails(const std::vector<ProgressEntrySnapshot> &allJobs);
		void renderJobContextMenu(const ProgressEntrySnapshot &snapshot);
		void renderDeleteConfirmationPopup();
		void requestDeleteSelectionConfirmation(const std::vector<ProgressEntrySnapshot> &allJobs);
		void removeFromHistory(const std::vector<JobId> &ids);
		void applySelection(const std::vector<VisibleJobRow> &rows, int rowIndex, JobId jobId);
		void updateFocusedSelectionFallback();
		void removeSelectedFromHistory();
		[[nodiscard]] int countFinishedSelected(const std::vector<ProgressEntrySnapshot> &allJobs) const;
		[[nodiscard]] static int countFinishedDescendants(const std::vector<ProgressEntrySnapshot> &allJobs, JobId parentId);
		[[nodiscard]] static int countAllDescendants(const std::vector<ProgressEntrySnapshot> &allJobs, JobId parentId);
		[[nodiscard]] bool isSelected(JobId jobId) const;
		[[nodiscard]] static const char *toStatusLabel(JobStatus status);
		[[nodiscard]] static const char *toStatusBadge(JobStatus status);
		[[nodiscard]] static ImVec4 toStatusColor(JobStatus status);
		[[nodiscard]] static const char *toPriorityLabel(JobPriority priority);
		[[nodiscard]] static std::string formatDurationHms(std::chrono::milliseconds duration);
		[[nodiscard]] static std::string formatEstimatedTime(const ProgressEntrySnapshot &snapshot);
		[[nodiscard]] static std::string formatTimePoint(const Time::TimePoint &timePoint);
		[[nodiscard]] static std::string formatDuration(const Time::TimePoint &startedAt, const Time::TimePoint &finishedAt);
		[[nodiscard]] static std::string buildDisplayLabel(const ProgressEntrySnapshot &snapshot);
	};
} // namespace DefectStudio
