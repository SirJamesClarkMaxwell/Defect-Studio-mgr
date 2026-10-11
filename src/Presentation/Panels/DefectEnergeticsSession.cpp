#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsSession.hpp"

#include <algorithm>
#include <format>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "IO/VaspRunRecordIO.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"

namespace DefectStudio
{
	// Walks the folder and reads every run's OUTCAR head and tail, reusing the cached record of a
	// run whose OUTCAR did not change. The walk alone takes minutes on the Okeanos mount.
	class VaspRunScanJob final : public IJob
	{
	public:
		VaspRunScanJob(Path root, std::vector<VaspRunRecord> cached)
			: m_Root(std::move(root)), m_Cached(std::move(cached)) {}

		[[nodiscard]] std::string GetName() const override { return "Skan obliczeń VASP"; }
		[[nodiscard]] std::string GetType() const override { return "energetics.scan"; }
		void Execute(JobContext &context) override
		{
			context.SetStage("Szukanie OUTCAR");
			const auto runs = VaspRunRecordIO::FindRuns(m_Root, [&] { return context.IsCancellationRequested(); });
			context.ThrowIfCancellationRequested();
			std::vector<VaspRunRecord> records;
			for (std::size_t i = 0; i < runs.size(); ++i)
			{
				context.ThrowIfCancellationRequested();
				context.SetProgress(static_cast<float>(i), static_cast<float>(runs.size()));
				context.SetMessage(runs[i].Utf8());
				const auto cached = std::find_if(m_Cached.begin(), m_Cached.end(),
					[&](const VaspRunRecord &record) { return record.directory == runs[i]; });
				records.push_back(cached != m_Cached.end() && VaspRunRecordIO::IsUpToDate(*cached)
					? *cached : VaspRunRecordIO::Read(runs[i]));
			}
			m_Result = std::move(records);
		}
		[[nodiscard]] std::optional<std::vector<VaspRunRecord>> &Result() noexcept { return m_Result; }

	private:
		Path m_Root;
		std::vector<VaspRunRecord> m_Cached;
		std::optional<std::vector<VaspRunRecord>> m_Result;
	};

	namespace
	{
		std::string Relative(const Path &path, const Path &root)
		{
			if (root.Empty())
				return path.Utf8();
			const auto relative = path.Native().lexically_relative(root.Native());
			return relative.empty() || relative.native().starts_with(L"..") ? path.Utf8()
				: Path(relative).Utf8();
		}
	} // namespace

	void DefectEnergeticsSession::SetPersistedState(EnergeticsFile file)
	{
		cancelJobs();
		InvalidateInputs();
		m_Concentrations = {};
		m_Continuations.clear();
		m_MuSet = 0;
		m_Changed = false;
		m_BulkDirectory = {};
		m_Edges.reset();
		m_BulkVolume.reset();
		*m_File = std::move(file);
		m_Message.clear();
		rebuild();
	}

	const FnvCorrectionsFile &DefectEnergeticsSession::fnvFile() const
	{
		static const FnvCorrectionsFile none;
		const auto panel = m_FnvPanel.lock(); // owned by EditorLayer for the whole session
		return panel ? panel->SavedCorrections() : none;
	}

	void DefectEnergeticsSession::rebuild()
	{
		m_Model = BuildEnergeticsModel(*m_File, fnvFile(), m_Edges);
		const Path bulk = m_Model.bulk ? m_File->runs[*m_Model.bulk].directory : Path{};
		if (bulk != m_BulkDirectory)
		{
			m_BulkDirectory = bulk;
			readBandEdges();
			m_Model = BuildEnergeticsModel(*m_File, fnvFile(), m_Edges);
		}
		m_ModelDirty = false;
		m_MuSet = std::clamp(m_MuSet, 0, std::max(0, static_cast<int>(m_Model.muSets.size()) - 1));
	}

	// Deferred until every view has released references into m_Model.rows.
	void DefectEnergeticsSession::Changed()
	{
		m_Changed = true;
		InvalidateInputs();
	}

	void DefectEnergeticsSession::readBandEdges()
	{
		m_Edges.reset();
		m_BulkVolume.reset();
		if (m_Model.bulk)
		{
			m_Edges = VaspRunRecordIO::ReadBandEdges(m_File->runs[*m_Model.bulk].directory);
			m_BulkVolume = VaspRunRecordIO::ReadCellVolume(m_File->runs[*m_Model.bulk].directory);
		}
	}

	EnergeticsChoice &DefectEnergeticsSession::choiceFor(const EnergeticsRow &row)
	{
		auto &choices = m_File->choices;
		const auto found = std::find_if(choices.begin(), choices.end(),
			[&](const EnergeticsChoice &choice) { return choice.defect == row.defect && choice.charge == row.charge; });
		if (found != choices.end())
			return *found;
		EnergeticsChoice choice;
		choice.defect = row.defect;
		choice.charge = row.charge;
		return choices.emplace_back(std::move(choice));
	}

	std::string DefectEnergeticsSession::runLabel(const std::size_t run) const
	{
		if (run >= m_File->runs.size())
			return "?";
		const VaspRunRecord &record = m_File->runs[run];
		std::string label = Relative(record.directory, m_File->root);
		if (!CalculationUsable(record))
			label += record.finished ? "  [niezbieżne]" : "  [przerwane]";
		return label;
	}

	void DefectEnergeticsSession::startScan()
	{
		const auto jobSystem = m_JobSystem.lock();
		if (m_ScanJob || !jobSystem || m_File->root.Empty())
			return;
		m_ScanJob = CreateRef<VaspRunScanJob>(m_File->root, m_File->runs);
		m_ScanJobId = jobSystem->Submit(m_ScanJob, JobPriority::Normal);
		m_Message.clear();
	}

	void DefectEnergeticsSession::pollScan()
	{
		if (!m_ScanJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_ScanJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_ScanJob->Result())
		{
			// Runs outside the root were added by hand (mu references): the walk cannot find them again.
			std::vector<VaspRunRecord> outside;
			for (auto &run : m_File->runs)
				if (Relative(run.directory, m_File->root) == run.directory.Utf8())
					outside.push_back(std::move(run));
			m_File->runs = std::move(*m_ScanJob->Result());
			m_File->runs.insert(m_File->runs.end(), outside.begin(), outside.end());
			std::size_t broken = 0;
			for (const auto &run : m_File->runs)
				broken += run.error.empty() ? 0 : 1;
			m_Message = std::format("Znaleziono {} obliczeń{}.", m_File->runs.size(),
				broken ? std::format(" ({} bez czytelnego OUTCAR)", broken) : "");
			m_BulkDirectory = {};
			m_Edges.reset();
			m_BulkVolume.reset();
			Changed();
		}
		else
			m_Message = snapshot && snapshot->status == JobStatus::Cancelled ? "Skan przerwany."
				: snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Skan nie powiódł się.";
		m_ScanJob.reset();
		m_ScanJobId = 0;
	}

	void DefectEnergeticsSession::cancelJobs()
	{
		if (const auto jobs = m_JobSystem.lock())
			for (const JobId id : {m_ScanJobId, m_FnvJobId, m_ConcentrationJobId})
				if (id != 0)
					(void)jobs->RequestCancel(id);
		// Detached jobs retain their own input copies; their results cannot reach the new project.
		m_ScanJob.reset();
		m_FnvJob.reset();
		m_ConcentrationJob.reset();
		m_ScanJobId = m_FnvJobId = m_ConcentrationJobId = 0;
		m_FnvQueue.clear();
		m_FnvRunning.reset();
		m_FnvDone = m_FnvTotal = 0;
	}

	void DefectEnergeticsSession::InvalidateInputs()
	{
		++m_InputRevision;
		m_ModelDirty = true;
		m_ConcentrationsCurrent = false;
	}

	void DefectEnergeticsSession::Update()
	{
		pollScan();
		pollFnvBatch();
		if (m_ModelDirty)
			rebuild();
		pollConcentrations();
	}

	void DefectEnergeticsSession::FlushChanges()
	{
		if (m_ModelDirty)
			rebuild();
		if (!std::exchange(m_Changed, false))
			return;
		if (m_SaveHandler)
			m_SaveHandler(*m_File);
	}
} // namespace DefectStudio
