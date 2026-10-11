#pragma once

#include <functional>
#include <optional>
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
		explicit CalculationSummarySession(WeakRef<JobSystem> jobSystem);
		~CalculationSummarySession();
		void OpenDirectory(Path directory);
		void Update();
		void SetShowViewsHandler(std::function<void()> handler) { m_ShowViews = std::move(handler); }
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
		std::function<std::optional<VaspRunRecordIO::BandEdges>()> m_BandEdgesProvider;
		std::optional<VaspRunRecordIO::BandEdges> m_PickedEdges; // from EIGENVAL of m_EdgesDirectory
		Path m_EdgesDirectory;
		std::function<void()> m_ShowViews;
	};
} // namespace DefectStudio
