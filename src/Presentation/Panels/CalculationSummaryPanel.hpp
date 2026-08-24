#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "ScientificRuntime/Python/VaspOutputBridge.hpp"

namespace DefectStudio
{
	class JobSystem;
	class VaspOutputJob;

	// Per-directory summary of a completed VASP calculation (docs/work/project/plans/
	// 2026-08-24-calc-tools.md section 5): energy convergence trend (plotted), final energy, band
	// gap, CPU/user/system/elapsed time, total drift, NELECT/ISPIN, pressure/stress, space group.
	// Loaded via the same VaspOutputBridge/VaspOutputJob ElectronicStructurePanel uses, extended
	// (not duplicated into a new bridge) - see the plan's explicit decision on this. Opened via
	// ProjectTreePanel's per-folder RMB "Show Calculation Summary" (ProjectEvents::
	// CalculationSummaryOpenRequested), same wiring shape as "Set as Bulk Reference". TextEditorPanel-
	// style: one panel instance, no tabs - opening a new directory replaces whatever was open.
	class CalculationSummaryPanel final : public IPanel
	{
	public:
		explicit CalculationSummaryPanel(
			WeakRef<JobSystem> jobSystem, std::string title = "Calculation Summary", bool visibleByDefault = false);
		CalculationSummaryPanel(const CalculationSummaryPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Targets `directory`, dispatches a fresh load (replacing any previously loaded directory/
		// data), and makes the panel visible.
		void OpenDirectory(Path directory);

	private:
		void dispatchLoad();
		void pollJob();
		void renderSummary(const VaspOutputData &data);

		WeakRef<JobSystem> m_JobSystem;
		Path m_Directory;
		std::optional<VaspOutputData> m_Data;
		std::string m_Error;
		Ref<VaspOutputJob> m_PendingJob;
		JobId m_PendingJobId = 0;
	};
} // namespace DefectStudio
