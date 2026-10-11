#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Defects/DefectConcentrations.hpp"
#include "IO/EnergeticsIO.hpp"
#include "Presentation/Panels/DefectEnergeticsModel.hpp"

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
		double gap = 0.0; // input snapshot, also used when displaying stale results
		std::string error;
	};

	// EditorLayer polls once before rendering all views and flushes once after them, even when hidden.
	class DefectEnergeticsSession
	{
	public:
		explicit DefectEnergeticsSession(WeakRef<JobSystem> jobs) : m_JobSystem(std::move(jobs)) {}
		~DefectEnergeticsSession() { cancelJobs(); }
		void Update();
		void FlushChanges();
		void SetPersistedState(EnergeticsFile file);
		void SetSaveHandler(std::function<void(const EnergeticsFile &)> handler) { m_SaveHandler = std::move(handler); }
		void SetFnvPanel(WeakRef<FnvCorrectionPanel> panel)
		{
			m_FnvPanel = std::move(panel);
			InvalidateInputs();
		}
		// EditorLayer: make every energetics view and the FNV panel visible.
		void SetShowViewsHandler(std::function<void()> handler) { m_ShowViews = std::move(handler); }
		void ShowViews() const
		{
			if (m_ShowViews)
				m_ShowViews();
		}
		void SetFnvStoreHandler(std::function<void(PersistedFnvCorrection)> handler) { m_StoreFnv = std::move(handler); }
		[[nodiscard]] const std::optional<VaspRunRecordIO::BandEdges> &BulkBandEdges() const { return m_Edges; }
		// FNV edits invalidate concentration inputs without dirtying energetics.yaml.
		void InvalidateInputs();
		// Views call this after editing persisted inputs; save is deferred until FlushChanges().
		void Changed();
		void HideDefect(const std::string &name);
		void RestoreDefect(const std::string &name);
		void AddDefectFolder(const Path &path);
		void RemoveExtraRoot(const Path &path);
		void StartConcentrations();
		[[nodiscard]] bool ConcentrationsCurrent() const { return m_ConcentrationsCurrent; }
		[[nodiscard]] const EnergeticsConcentrations &Concentrations() const { return m_Concentrations; }

	private:
		friend class DefectEnergeticsPanel;
		void cancelJobs();
		void rebuild();
		void readBandEdges();
		void startScan();
		void pollScan();
		void startFnvBatch();
		void submitNextFnv();
		void pollFnvBatch();
		void pollConcentrations();
		[[nodiscard]] const FnvCorrectionsFile &fnvFile() const;
		[[nodiscard]] EnergeticsChoice &choiceFor(const EnergeticsRow &row);
		[[nodiscard]] std::string runLabel(std::size_t run) const;
		[[nodiscard]] std::string concentrationsTsv() const;
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
		int m_MuSet = 0;
		bool m_Changed = false;
		std::set<std::pair<std::string, int>> m_Continuations;
		std::function<void(PersistedFnvCorrection)> m_StoreFnv;
		std::function<void()> m_ShowViews;
		Path m_BulkDirectory;
		bool m_ModelDirty = true;
		std::uint64_t m_InputRevision = 0, m_SubmittedRevision = 0;
		bool m_ConcentrationsCurrent = false;
	};
} // namespace DefectStudio
