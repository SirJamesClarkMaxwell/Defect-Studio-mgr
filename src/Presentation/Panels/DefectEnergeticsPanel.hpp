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
#include "IO/EnergeticsIO.hpp"
#include "Presentation/Panels/DefectEnergeticsModel.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class JobSystem;
	class FnvCorrectionPanel;
	class FnvCorrectionJob;
	class VaspRunScanJob;

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
		void startScan();
		void pollScan();
		void startFnvBatch();
		void pollFnvBatch();
		void submitNextFnv();
		void readBandEdges();
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

		int m_MuSet = 0;
		std::array<char, 64> m_NewPhaseFormula{};
		std::array<char, 64> m_NewSetName{};
	};
} // namespace DefectStudio
