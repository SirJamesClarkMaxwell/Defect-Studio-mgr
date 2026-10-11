#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/VaspRunRecordIO.hpp"
#include "ScientificRuntime/Python/VaspOutputBridge.hpp"

namespace DefectStudio
{
	class JobSystem;
	class VaspOutputJob;

	class CalculationSummarySession
	{
	public:
		explicit CalculationSummarySession(WeakRef<JobSystem> jobSystem, bool restoreCache = true);
		~CalculationSummarySession();
		void OpenDirectory(Path directory);
		void Update();
		[[nodiscard]] const Path &Directory() const { return m_Directory; }
		// EditorLayer's existing main-thread update also maintains runtime summary instances.
		void SetUpdateInstancesHandler(std::function<void()> handler) { m_UpdateInstances = std::move(handler); }
		void SetAvailableBands(const std::set<int> &bands);
		[[nodiscard]] const std::map<int, bool> &BandSelection() const { return m_BandSelection; }
		void SetBandSelected(int band, bool selected);
		void SetSpinSelected(int spin, bool selected);
		[[nodiscard]] bool IsKsLevelSelected(int spin, int band) const;
		void SetBandEdgesProvider(std::function<std::optional<VaspRunRecordIO::BandEdges>()> provider)
		{ m_BandEdgesProvider = std::move(provider); }

	private:
		friend class CalculationSummaryPanel;
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
		[[nodiscard]] Ref<CalculationSummarySession> clone() const;
		void cancelJobs();
		void dispatchRun(Run &run, int bandStart, int bandEnd);
		[[nodiscard]] bool isLoading() const;
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
		std::map<int, bool> m_BandSelection;
		std::array<bool, 2> m_SpinSelected{true, true};
		std::function<std::optional<VaspRunRecordIO::BandEdges>()> m_BandEdgesProvider;
		std::optional<VaspRunRecordIO::BandEdges> m_PickedEdges; // from EIGENVAL of m_EdgesDirectory
		Path m_EdgesDirectory;
		std::function<void()> m_UpdateInstances;
	};
} // namespace DefectStudio
