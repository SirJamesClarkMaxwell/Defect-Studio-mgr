#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Defects/DefectConcentrations.hpp"
#include "IO/EnergeticsIO.hpp"
#include "Presentation/Panels/DefectEnergeticsModel.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class JobSystem;
	class FnvCorrectionPanel;
	class FnvCorrectionJob;
	class VaspRunScanJob;
	class ConcentrationJob;

	// Equilibrium concentrations of the Defect Energetics panel, computed in a job over T x lambda
	// (lambda: position on the Delta mu line between two sets).
	struct EnergeticsConcentrations
	{
		std::vector<std::string> defects;
		std::vector<double> temperatures; // K
		std::vector<double> lambdas;      // 0..1 along the line, {0} = the chosen set only
		std::vector<std::string> muLabels; // per lambda
		std::vector<DefectConcentrationSolution> solutions; // [lambda][T], T fastest
		std::vector<ConcentrationDefects> intercepts;       // per lambda (A_q, N_sites, g)
		std::string error;
	};

	// task/99: "Energetyka defektów" - point at a folder of VASP runs, pick the converged run per
	// (defect, q) (proposed automatically, never overridden), compute the missing FNV corrections in
	// a queue, choose the mu references, competing phases and mu sets, see E_f(E_F) with its
	// envelope and transition levels, and export everything as TSV.
	// Plan: docs/work/project/plans/2026-10-11-energetics-auto-and-mu.md.
	class DefectEnergeticsPanel final : public IPanel
	{
	public:
		explicit DefectEnergeticsPanel(WeakRef<JobSystem> jobSystem, std::string title = "Energetyka defektów",
			bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// The project's energetics.yaml (EditorLayer, on project open). Does not call the handler.
		void SetPersistedState(EnergeticsFile file);
		void SetSaveHandler(std::function<void(const EnergeticsFile &)> handler) { m_SaveHandler = std::move(handler); }
		// Where the FNV corrections are read from and the batch results go.
		void SetFnvPanel(WeakRef<FnvCorrectionPanel> panel) { m_FnvPanel = std::move(panel); }

	private:
		void drawSource();
		void drawRows();
		void drawChemicalPotentials();
		void drawFormation();
		void drawExport();
		void drawConcentrations();
		void startConcentrations();
		void pollConcentrations();
		[[nodiscard]] std::string concentrationsTsv() const;
		// Valence band (red, below 0) and conduction band (blue, above gap) fading from their edges,
		// inside the current ImPlot plot; `alongX`: the energy is the x axis.
		static void drawBands(double gap, bool alongX);
		void startScan();
		void pollScan();
		void startFnvBatch();
		void pollFnvBatch();
		void submitNextFnv();
		void readBandEdges();
		void pickReference(const std::string &species); // mu_ref from a run folder outside the root
		void changed(); // rebuild the model and save
		void rebuild();
		[[nodiscard]] const FnvCorrectionsFile &fnvFile() const;
		[[nodiscard]] EnergeticsChoice &choiceFor(const EnergeticsRow &row);
		[[nodiscard]] std::string runLabel(std::size_t run) const;

		WeakRef<JobSystem> m_JobSystem;
		WeakRef<FnvCorrectionPanel> m_FnvPanel;
		std::function<void(const EnergeticsFile &)> m_SaveHandler;
		Ref<EnergeticsFile> m_File = CreateRef<EnergeticsFile>(); // shared with Clone()s
		EnergeticsModel m_Model;
		std::optional<VaspRunRecordIO::BandEdges> m_Edges;
		std::string m_Message;

		Ref<VaspRunScanJob> m_ScanJob;
		JobId m_ScanJobId = 0;

		// FNV queue: one LOCPOT at a time (two LOCPOT reads, ~1 min locally, minutes over SSHFS).
		struct FnvTask
		{
			std::string defect;
			int charge = 0;
			PersistedFnvCorrection entry; // inputs; profiles and windows filled on completion
		};
		std::deque<FnvTask> m_FnvQueue;
		std::optional<FnvTask> m_FnvRunning;
		Ref<FnvCorrectionJob> m_FnvJob;
		JobId m_FnvJobId = 0;
		std::size_t m_FnvDone = 0;
		std::size_t m_FnvTotal = 0;

		std::optional<double> m_BulkVolume; // A^3, CONTCAR of the bulk
		Ref<ConcentrationJob> m_ConcentrationJob;
		JobId m_ConcentrationJobId = 0;
		EnergeticsConcentrations m_Concentrations;
		int m_ConcentrationLambda = 0;
		int m_ConcentrationTemperature = 0;
		int m_ConcentrationMap = 0; // 0 = E_F, k > 0 = log10 c of defect k-1

		int m_MuSet = 0;
		bool m_ShowContinuations = false; // every charge line over the gap, not only the envelope
		std::array<char, 64> m_NewPhaseFormula{};
		std::array<char, 64> m_NewSetName{};
	};
} // namespace DefectStudio
