#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <filesystem>
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/PlatformPaths.hpp"
#include "IO/PhononAnalysisIO.hpp"
#include "IO/CcdAnalysisIO.hpp"

namespace DefectStudio
{
	class AnalysisDatasetScanJob final : public IJob
	{
	public:
		explicit AnalysisDatasetScanJob(std::vector<Path> roots) : m_Roots(std::move(roots)) {}
		std::string GetName() const override { return "Skan zestawów analiz"; }
		std::string GetType() const override { return "phonon.datasets.scan"; }
		void Execute(JobContext &context) override
		{
			std::vector<AnalysisDatasetRef> found;
			context.SetStage("Szukanie zestawów");
			for (std::size_t i = 0; i < m_Roots.size(); ++i)
			{
				context.ThrowIfCancellationRequested();
				context.SetMessage(m_Roots[i].Utf8());
				context.SetProgress(static_cast<float>(i), static_cast<float>(m_Roots.size()));
				auto datasets = FindDatasets(m_Roots[i], [&] { return context.IsCancellationRequested(); });
				found.insert(found.end(), datasets.begin(), datasets.end());
			}
			context.ThrowIfCancellationRequested();
			m_Result = std::move(found);
		}
		std::optional<std::vector<AnalysisDatasetRef>> &Result() noexcept { return m_Result; }
	private:
		std::vector<Path> m_Roots;
		std::optional<std::vector<AnalysisDatasetRef>> m_Result;
	};

	namespace
	{
		// One spelling per directory: "a/b" and "a\b" must hit the same cache entry.
		std::string SpectraKey(const Path &directory)
		{
			const auto text = directory.Native().lexically_normal().generic_u8string();
			return std::string(text.begin(), text.end());
		}
	}

	PhononPanelSession::~PhononPanelSession() { cancelJob(); cancelFigureJob(); CancelDatasetScan(); }

	void PhononPanelSession::SetDatasets(std::vector<AnalysisDatasetRef> datasets)
	{
		m_CaseOutputDirectories.clear();
		++m_PlotRevision;
		std::sort(datasets.begin(), datasets.end(), [](const auto &a, const auto &b) { return a.directory.Native() < b.directory.Native(); });
		m_Datasets.clear();
		for (auto &dataset : datasets)
			if (std::none_of(m_Datasets.begin(), m_Datasets.end(), [&](const auto &existing) {
				return SameFilePath(existing.directory, dataset.directory, {});
			})) m_Datasets.push_back(std::move(dataset));
		m_Spectra.clear();
		m_CcdResults.clear();
		if (!m_Datasets.empty()) selectCaseDispersion();
	}

	void PhononPanelSession::AddDataset(AnalysisDatasetRef dataset)
	{
		m_CaseOutputDirectories.clear();
		++m_PlotRevision;
		const auto found = std::find_if(m_Datasets.begin(), m_Datasets.end(), [&](const auto &entry) {
			return SameFilePath(entry.directory, dataset.directory, {});
		});
		m_Spectra.erase(SpectraKey(dataset.directory));
		m_CcdResults.erase(SpectraKey(dataset.directory));
		if (found == m_Datasets.end()) m_Datasets.push_back(std::move(dataset));
		else *found = std::move(dataset);
		std::sort(m_Datasets.begin(), m_Datasets.end(), [](const auto &a, const auto &b) { return a.directory.Native() < b.directory.Native(); });
	}

	bool PhononPanelSession::IsCompared(const Path &directory) const
	{
		return std::any_of(m_Settings.comparedDirectories.begin(), m_Settings.comparedDirectories.end(),
			[&](const auto &path) { return SameFilePath(path, directory, {}); });
	}

	void PhononPanelSession::SetCompared(const Path &directory, bool compared)
	{
		const bool wasCompared = IsCompared(directory);
		if (!compared)
		{
			std::erase_if(m_Settings.comparedDirectories, [&](const auto &path) { return SameFilePath(path, directory, {}); });
			if (wasCompared) { ++m_PlotRevision; Changed(); }
			return;
		}
		if (IsCompared(directory)) return;
		if (std::any_of(m_Datasets.begin(), m_Datasets.end(), [&](const auto &entry) {
			return entry.kind != AnalysisDatasetKind::Dispersion && entry.kind != AnalysisDatasetKind::BulkMode && SameFilePath(entry.directory, directory, {});
		}))
		{
			m_Settings.comparedDirectories.push_back(directory);
			++m_PlotRevision;
			Changed();
		}
	}

	bool PhononPanelSession::SelectDataset(const Path &directory)
	{
		const auto found = std::find_if(m_Datasets.begin(), m_Datasets.end(), [&](const auto &entry) {
			return SameFilePath(entry.directory, directory, {});
		});
		if (found == m_Datasets.end()) return false;
		if (found->kind == AnalysisDatasetKind::Phonons) m_Settings.resultDirectory = found->directory;
		else if (found->kind == AnalysisDatasetKind::Ccd) m_Settings.selectedCcdDirectory = found->directory;
		else if (found->kind == AnalysisDatasetKind::BulkMode) m_Settings.selectedBulkModeDirectory = found->directory;
		else m_Settings.selectedDispersionDirectory = found->directory;
		m_CurrentDataset = found->directory;
		return true;
	}

	std::optional<std::reference_wrapper<const PhononAnalysisResult>> PhononPanelSession::Spectra(
		const Path &directory, std::string &outError)
	{
		auto [it, inserted] = m_Spectra.try_emplace(SpectraKey(directory));
		auto &result = it->second.result;
		auto &error = it->second.error;
		if (inserted)
		{
			PhononAnalysisResult spectra;
			if (PhononAnalysisIO::LoadSpectra(directory, spectra, error)) result = std::move(spectra);
		}
		outError = error;
		return result ? std::optional<std::reference_wrapper<const PhononAnalysisResult>>(std::cref(*result)) : std::nullopt;
	}

	void PhononPanelSession::cacheActiveResult()
	{
		if (!m_Result || m_Settings.resultDirectory.Empty()) return;
		auto &cached = m_Spectra[SpectraKey(m_Settings.resultDirectory)];
		cached.result = *m_Result;
		cached.error.clear();
		cached.modesRequested = true;
		cached.modesError.clear();
	}

	std::optional<std::reference_wrapper<const PhononAnalysisResult>> PhononPanelSession::PlotModes(
		const Path &directory, std::string &outError)
	{
		(void)Spectra(directory, outError);
		auto &cached = m_Spectra.at(SpectraKey(directory));
		// Upgrade the existing spectra cache only when a mode plot needs the larger result.
		if (!cached.modesRequested)
		{
			cached.modesRequested = true;
			PhononAnalysisResult result;
			if (PhononAnalysisIO::Load(directory, result, cached.modesError)) cached.result = std::move(result);
		}
		outError = cached.modesError;
		if (!cached.result || cached.result->modes.empty()) return std::nullopt;
		return std::cref(*cached.result);
	}

	std::optional<std::reference_wrapper<const CcdAnalysisResult>> PhononPanelSession::Ccd(
		const Path &directory, std::string &outError)
	{
		auto [it, inserted] = m_CcdResults.try_emplace(SpectraKey(directory));
		auto &[result, error, modified] = it->second;
		std::array<std::filesystem::file_time_type, 2> timestamps;
		std::size_t index = 0;
		for (const auto name : {"summary.json", "ccd_displacement.json"})
		{
			std::error_code ignored;
			timestamps[index++] = std::filesystem::last_write_time((directory / name).Native(), ignored);
		}
		if (inserted || modified != timestamps)
		{
			if (!inserted) ++m_PlotRevision;
			modified = timestamps;
			result.reset(); error.clear();
			CcdAnalysisResult ccd;
			if (CcdAnalysisIO::Load(directory, ccd, error)) result = std::move(ccd);
		}
		outError = error;
		return result ? std::optional<std::reference_wrapper<const CcdAnalysisResult>>(std::cref(*result)) : std::nullopt;
	}

	void PhononPanelSession::StartDatasetScan(WeakRef<JobSystem> jobs, std::vector<Path> roots)
	{
		CancelDatasetScan();
		m_ScanJobs = std::move(jobs);
		m_ScanMessage.clear();
		std::erase_if(roots, [](const auto &root) { return root.Empty(); });
		std::sort(roots.begin(), roots.end(), [](const auto &a, const auto &b) { return a.Native() < b.Native(); });
		roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
		if (roots.empty()) return;
		const auto system = m_ScanJobs.lock();
		if (!system) { m_ScanMessage = "JobSystem niedostępny."; return; }
		m_ScanJob = CreateRef<AnalysisDatasetScanJob>(std::move(roots));
		m_ScanJobId = system->Submit(m_ScanJob, JobPriority::Normal);
	}

	void PhononPanelSession::CancelDatasetScan()
	{
		if (m_ScanJob)
			if (const auto system = m_ScanJobs.lock()) (void)system->RequestCancel(m_ScanJobId);
		m_ScanJob.reset(); m_ScanJobId = 0;
	}

	bool PhononPanelSession::PollDatasetScan()
	{
		if (!m_ScanJob) return false;
		const auto system = m_ScanJobs.lock();
		const auto snapshot = system ? system->GetJob(m_ScanJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return false;
		const bool completed = snapshot && snapshot->status == JobStatus::Completed && m_ScanJob->Result();
		if (completed)
		{
			auto datasets = std::move(*m_ScanJob->Result());
			// Explicit selections also survive when their directories are outside scanned roots.
			auto pinned = m_Settings.comparedDirectories;
			pinned.push_back(m_Settings.resultDirectory); pinned.push_back(m_Settings.selectedDispersionDirectory);
			pinned.push_back(m_Settings.selectedCcdDirectory);
			pinned.push_back(m_Settings.selectedBulkModeDirectory);
			for (const auto &item : Cases())
			{
				pinned.push_back(item.settings.selectedDispersionDirectory);
				pinned.push_back(item.settings.selectedBulkModeDirectory);
				pinned.insert(pinned.end(), item.datasets.begin(), item.datasets.end());
			}
			for (const auto &path : pinned)
				if (!path.Empty())
					if (auto ref = ReadDatasetRef(path)) datasets.push_back(std::move(*ref));
			SetDatasets(std::move(datasets));
			m_ScanMessage = "Skan zakończony.";
		}
		else m_ScanMessage = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Skan przerwany.";
		m_ScanJob.reset(); m_ScanJobId = 0;
		return completed;
	}

	const std::vector<double> &PhononPanelSession::PlCurve(const PhononAnalysisResult &result, std::string_view choice)
	{
		if (choice == "formula") return result.plFormula;
		if (choice == "spin_purified") return result.plSpinPurified;
		if (choice == "experimental") return result.plExperimental;
		return result.plComputed;
	}

	std::string PhononPanelSession::PlCurveChoice(const PhononAnalysisResult &result, std::string_view preferred)
	{
		if (!PlCurve(result, preferred).empty()) return std::string(preferred);
		for (const auto key : {"computed", "spin_purified", "experimental", "formula"})
			if (!PlCurve(result, key).empty()) return key;
		return "computed";
	}

	bool PhononPanelSession::OutputContainsInput(const Path &output, const Path &input)
	{
		if (input.Empty()) return false;
		if (output.Empty()) return true;
		std::error_code error;
		const auto outer = FileSystem::WeaklyCanonical(output.Native(), error);
		if (error) return true;
		const auto inner = FileSystem::WeaklyCanonical(input.Native(), error);
		if (error) return true;
		auto a = outer.begin(), b = inner.begin();
		for (; a != outer.end() && b != inner.end() && Platform::FileSystemNamesEqual(*a, *b); ++a, ++b) {}
		return a == outer.end();
	}
}
