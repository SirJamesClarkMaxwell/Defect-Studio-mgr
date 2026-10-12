#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include "Core/JobSystem/JobSystem.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"

namespace DefectStudio
{
	Path PhononModesPanel::ccdOutputPath() const
	{
		const auto &settings = m_Session->Settings();
		if (!settings.ccdOutputDirectory.Empty()) return settings.ccdOutputDirectory;
		if (m_ProjectDirectory.Empty() || settings.excitedStateDirectory.Empty()) return {};
		return m_ProjectDirectory.Join("ccd") / settings.excitedStateDirectory.filename();
	}

	void PhononModesPanel::submitCcd()
	{
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
		{
			m_CcdError = "JobSystem niedostępny.";
			return;
		}
		auto &settings = m_Session->Settings();
		settings.ccdOutputDirectory = ccdOutputPath();
		m_PendingCcdJob = CreateRef<CcdInputsJob>(settings.groundStateDirectory, settings.excitedStateDirectory,
			settings.ccdOutputDirectory, settings.ccdStep);
		m_PendingCcdJobId = jobs->Submit(m_PendingCcdJob, JobPriority::Normal);
		m_CcdResult.clear();
		m_CcdError.clear();
		persist();
	}

	void PhononModesPanel::pollCcdJob()
	{
		if (!m_PendingCcdJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingCcdJob->GetResult())
			m_CcdResult = *m_PendingCcdJob->GetResult();
		else if (snapshot && snapshot->status == JobStatus::Failed && m_PendingCcdJob->GetError())
			m_CcdError = m_PendingCcdJob->GetError()->userMessage;
		else
			m_CcdError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Generowanie CCD przerwane.";
		m_PendingCcdJob.reset();
		m_PendingCcdJobId = 0;
	}
}
