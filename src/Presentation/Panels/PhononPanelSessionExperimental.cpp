#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	class ExperimentalSpectrumJob final : public IJob
	{
	public:
		explicit ExperimentalSpectrumJob(ExperimentalSpectrumSource source) : m_Source(std::move(source)) {}
		std::string GetName() const override { return "Wczytywanie PL eksperymentalnego"; }
		std::string GetType() const override { return "phonon.experimental.load"; }
		void Execute(JobContext &context) override
		{
			context.ThrowIfCancellationRequested();
			context.SetMessage(m_Source.file.Utf8());
			std::string text;
			if (!TextFileIO::Load(m_Source.file, text, m_Data.error)) return;
			context.ThrowIfCancellationRequested();
			std::size_t end = 0;
			for (int line = 0; line < 8 && end < text.size(); ++line)
			{
				const auto newline = text.find('\n', end);
				end = newline == std::string::npos ? text.size() : newline + 1;
			}
			m_Data.preview = text.substr(0, end);
			if (!ParseDataTable(text, m_Data.table, m_Data.error)) return;
			context.ThrowIfCancellationRequested();
			if (m_Source.xColumn < 0 || m_Source.yColumn < 0 ||
				static_cast<std::size_t>(std::max(m_Source.xColumn, m_Source.yColumn)) >= m_Data.table.columns.size())
			{
				m_Data.error = "Wybrana kolumna nie istnieje w pliku.";
				return;
			}
			const auto unit = ExperimentalSpectrumUnit(m_Source.unit);
			if (!unit) { m_Data.error = "Nieznana jednostka: " + m_Source.unit; return; }
			ExperimentalSpectrum spectrum;
			if (BuildExperimentalSpectrum(m_Data.table.columns[m_Source.xColumn], m_Data.table.columns[m_Source.yColumn],
				*unit, spectrum, m_Data.error)) m_Data.spectrum = std::move(spectrum);
			context.ThrowIfCancellationRequested();
		}
		PhononExperimentalData &Data() { return m_Data; }
	private:
		ExperimentalSpectrumSource m_Source;
		PhononExperimentalData m_Data;
	};

	const PhononExperimentalData &PhononPanelSession::Experimental(const ExperimentalSpectrumSource &source)
	{
		const auto file = Path::FromResolved((source.file.Native().is_absolute() ? source.file.Native() :
			m_ProjectDirectory.Native() / source.file.Native()).lexically_normal());
		const auto key = std::tuple{file.Utf8(), source.xColumn, source.yColumn, source.unit};
		auto [it, inserted] = m_ExperimentalSpectra.try_emplace(key);
		auto &cached = it->second;
		std::error_code error;
		const auto modified = std::filesystem::last_write_time(file.Native(), error);
		if (inserted || cached.modified != modified)
		{
			if (cached.jobId) cancelJob(cached.jobId);
			cached = {};
			cached.modified = modified;
			++m_PlotRevision;
			if (error) cached.data.error = error.message();
			else if (const auto jobs = m_JobSystem.lock())
			{
				auto resolved = source;
				resolved.file = file;
				cached.job = CreateRef<ExperimentalSpectrumJob>(std::move(resolved));
				try
				{
					cached.jobId = jobs->Submit(cached.job, JobPriority::Low);
					cached.data.loading = true;
				}
				catch (const std::exception &exception) { cached.data.error = exception.what(); cached.job.reset(); }
			}
			else cached.data.error = "JobSystem niedostępny.";
		}
		PollExperimentalSpectra();
		return cached.data;
	}

	void PhononPanelSession::PollExperimentalSpectra()
	{
		const auto jobs = m_JobSystem.lock();
		for (auto &[key, cached] : m_ExperimentalSpectra)
			if (cached.jobId)
			{
				const auto snapshot = jobs ? jobs->GetJob(cached.jobId) : std::nullopt;
				if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) continue;
				// GetJob synchronizes with completion; only this main-thread path publishes the data.
				if (snapshot && snapshot->status == JobStatus::Completed) cached.data = std::move(cached.job->Data());
				else cached.data.error = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Wczytywanie przerwane.";
				cached.data.loading = false;
				cached.job.reset(); cached.jobId = 0;
				++m_PlotRevision;
			}
	}

	bool PhononPanelSession::AddExperimentalSpectrum(ExperimentalSpectrumSource source)
	{
		if (!Experimental(source).spectrum) return false;
		if (Cases().empty()) (void)AddCase({});
		for (auto &item : m_Settings.cases)
			if (item.id == ActiveCaseId())
			{
				if (source.label.empty()) source.label = source.file.filename().Utf8();
				item.experimentalSpectra.push_back(std::move(source));
				++m_PlotRevision;
				Changed();
				return true;
			}
		return false;
	}

	void PhononPanelSession::clearExperimentalSpectra()
	{
		for (const auto &[key, cached] : m_ExperimentalSpectra)
			if (cached.jobId) cancelJob(cached.jobId);
		m_ExperimentalSpectra.clear();
		m_ExperimentalImport = {};
		m_ExperimentalImportCase.clear();
	}
}
