#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsSession.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "IO/VaspRunRecordIO.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"

namespace DefectStudio
{
	// Walks the folders and reads every run's OUTCAR head and tail, reusing the cached record of a
	// run whose OUTCAR did not change. The walk alone takes minutes on the Okeanos mount.
	class VaspRunScanJob final : public IJob
	{
	public:
		VaspRunScanJob(std::vector<Path> roots, std::vector<VaspRunRecord> cached)
			: m_Roots(std::move(roots)), m_Cached(std::move(cached)) {}

		[[nodiscard]] std::string GetName() const override { return "Skan obliczeń VASP"; }
		[[nodiscard]] std::string GetType() const override { return "energetics.scan"; }
		void Execute(JobContext &context) override
		{
			context.SetStage("Szukanie OUTCAR");
			std::vector<Path> runs;
			for (const Path &root : m_Roots)
			{
				context.ThrowIfCancellationRequested();
				const auto found = VaspRunRecordIO::FindRuns(root, [&] { return context.IsCancellationRequested(); });
				for (const Path &path : found)
					runs.push_back(Path::FromResolved(path.Native().lexically_normal()));
			}
			std::sort(runs.begin(), runs.end(), [](const Path &a, const Path &b) { return a.Native() < b.Native(); });
			runs.erase(std::unique(runs.begin(), runs.end()), runs.end());
			context.ThrowIfCancellationRequested();
			std::vector<VaspRunRecord> records;
			for (std::size_t i = 0; i < runs.size(); ++i)
			{
				context.ThrowIfCancellationRequested();
				context.SetProgress(static_cast<float>(i), static_cast<float>(runs.size()));
				context.SetMessage(runs[i].Utf8());
				const auto cached = std::find_if(m_Cached.begin(), m_Cached.end(),
					[&](const VaspRunRecord &record) { return record.directory.Native().lexically_normal() == runs[i].Native(); });
				records.push_back(cached != m_Cached.end() && VaspRunRecordIO::IsUpToDate(*cached)
					? *cached : VaspRunRecordIO::Read(runs[i]));
			}
			m_Result = std::move(records);
		}
		[[nodiscard]] std::optional<std::vector<VaspRunRecord>> &Result() noexcept { return m_Result; }
		[[nodiscard]] const std::vector<Path> &Roots() const noexcept { return m_Roots; }

	private:
		std::vector<Path> m_Roots;
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
		// Old concentration snapshots must not keep a removed defect visible or exportable.
		if (std::any_of(m_Concentrations.defects.begin(), m_Concentrations.defects.end(), [&](const auto &defect) {
			return std::none_of(m_Model.rows.begin(), m_Model.rows.end(), [&](const auto &row) { return row.defect == defect; });
		}))
			m_Concentrations = {};
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

	void DefectEnergeticsSession::HideDefect(const std::string &name)
	{
		auto &hidden = m_File->hiddenDefects;
		if (name.empty() || std::find(hidden.begin(), hidden.end(), name) != hidden.end())
			return;
		hidden.push_back(name);
		Changed();
	}

	void DefectEnergeticsSession::RestoreDefect(const std::string &name)
	{
		// Copy before erasing: the caller may hold a reference into hiddenDefects.
		const std::string restored = name;
		if (std::erase(m_File->hiddenDefects, restored) != 0)
			Changed();
	}

	void DefectEnergeticsSession::AddDefectFolder(const Path &path)
	{
		if (path.Empty()) return;
		const std::array single{path};
		const auto normal = CalculationRoot(path, single);
		if (!normal) return;
		const std::array primary{m_File->root};
		if (!CalculationRoot(*normal, primary) &&
			std::none_of(m_File->extraRoots.begin(), m_File->extraRoots.end(), [&](const Path &root) {
				const std::array candidate{root};
				return CalculationRoot(root, candidate) == normal;
			}))
		{
			m_File->extraRoots.push_back(*normal);
			Changed();
		}
		startScan();
	}

	void DefectEnergeticsSession::RemoveExtraRoot(const Path &path)
	{
		const std::array single{path};
		const auto normal = CalculationRoot(path, single);
		if (!normal) return;
		if (std::erase_if(m_File->extraRoots, [&](const Path &root) {
			const std::array candidate{root};
			return CalculationRoot(root, candidate) == normal;
		}) == 0)
			return;
		Changed();
		if (m_ScanJob)
			startScan();
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
		if (!jobSystem)
			return;
		if (m_ScanJob)
			(void)jobSystem->RequestCancel(m_ScanJobId);
		m_ScanJob.reset();
		m_ScanJobId = 0;
		std::vector<Path> roots = m_File->extraRoots;
		if (!m_File->root.Empty()) roots.push_back(m_File->root);
		std::erase_if(roots, [](const Path &root) { return root.Empty(); });
		if (roots.empty()) return;
		m_ScanJob = CreateRef<VaspRunScanJob>(std::move(roots), m_File->runs);
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
			// Runs outside all scanned roots were added by hand (mu references).
			std::vector<VaspRunRecord> outside;
			for (auto &run : m_File->runs)
				if (!CalculationRoot(run.directory, m_ScanJob->Roots()))
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
