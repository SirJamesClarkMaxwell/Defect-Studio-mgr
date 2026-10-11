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

	// Opening a Project Tree selection starts a new chain. Continuations are session-only;
	// the existing single-directory startup cache remains independent of that chain.
	class CalculationSummaryPanel final : public IPanel
	{
	public:
		explicit CalculationSummaryPanel(
			WeakRef<JobSystem> jobSystem, std::string title = "Calculation Summary", bool visibleByDefault = false);
		CalculationSummaryPanel(const CalculationSummaryPanel &other) = default;

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Targets `directory`, dispatches a fresh load (replacing any previously loaded directory/
		// data), and makes the panel visible.
		void OpenDirectory(Path directory);

	private:
		struct Run
		{
			Path directory;
			std::string outcarName = "OUTCAR";
			std::optional<VaspOutputData> data;
			std::string error;
			Ref<VaspOutputJob> job;
			JobId jobId = 0;
		};

		void dispatchLoad();
		void dispatchRun(Run &run, int bandStart, int bandEnd);
		void pollJob();
		void renderChain();
		void renderPlots();
		void renderBoundaries();
		[[nodiscard]] bool isLoading() const;
		void renderSummary(const VaspOutputData &data);

		WeakRef<JobSystem> m_JobSystem;
		Path m_Directory;
		std::vector<Run> m_Chain;
		std::string m_Error;
		int m_DetailRun = 0;
		int m_BandStart = 1;
		int m_BandEnd = 18;
		int m_Kpoint = 1;
		bool m_AutoBands = true;
		bool m_SortPending = false;
		bool m_RelativeToFermi = false;
	};
} // namespace DefectStudio
