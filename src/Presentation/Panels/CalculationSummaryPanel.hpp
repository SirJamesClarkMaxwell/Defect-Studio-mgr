#pragma once

#include <functional>
#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/VaspRunRecordIO.hpp"
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
		// VBM/CBM drawn on the KS plot when no bulk was picked here (EditorLayer: Defect Energetics' bulk).
		void SetBandEdgesProvider(std::function<std::optional<VaspRunRecordIO::BandEdges>()> provider)
		{
			m_BandEdgesProvider = std::move(provider);
		}

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
		std::function<std::optional<VaspRunRecordIO::BandEdges>()> m_BandEdgesProvider;
		std::optional<VaspRunRecordIO::BandEdges> m_PickedEdges; // from EIGENVAL of m_EdgesDirectory
		Path m_EdgesDirectory;
		float m_ConvergenceHeight = 240.0f, m_KsHeight = 360.0f; // drag handles under the plots
	};
} // namespace DefectStudio
