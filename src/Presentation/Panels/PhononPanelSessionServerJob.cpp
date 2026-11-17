#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <chrono>
#include <format>
#include "Core/JobSystem/JobSystem.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	Path PhononPanelSession::DefaultServerJobDirectory() const
	{
		Path source;
		for (const auto &item : Cases())
			if (item.id == ActiveCaseId()) source = item.sourceDirectory;
		if (source.Empty()) source = Settings().forceConstantDirectory.parent_path();
		const auto name = std::format("server_job_{:%Y%m%d_%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		// No write probe in calculation trees: permission failures are handled by the generator.
		return DefaultDatasetDirectory({source}, name, m_ProjectDirectory, [](const Path &directory) {
			std::error_code error;
			const auto permissions = std::filesystem::status(directory.Native(), error).permissions();
			return !error && (permissions & (std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
				std::filesystem::perms::others_write)) != std::filesystem::perms::none;
		});
	}

	ServerJobRequest PhononPanelSession::ServerJobInputs() const
	{
		ServerJobRequest request;
		const auto &s = Settings();
		auto &inputs = request.inputs;
		// The local computation builder also probes its output tree; generation has its own chosen folder.
		inputs.forceConstantDirectory = s.forceConstantDirectory;
		inputs.groundStateDirectory = s.groundStateDirectory;
		inputs.excitedStateDirectory = s.excitedStateDirectory;
		if (!s.tripletDirectory.Empty()) inputs.tripletDirectory = s.tripletDirectory;
		inputs.groundAtExcitedDirectory = s.groundAtExcitedDirectory;
		inputs.excitedAtGroundDirectory = s.excitedAtGroundDirectory;
		inputs.zplFormula = s.zplFormula;
		inputs.bulkForceConstantDirectory = s.bulkForceConstantDirectory;
		inputs.hrMethod = s.hrMethod;
		inputs.hrCutoffAngstrom = s.hrCutoffAngstrom;
		inputs.dcutoffAngstrom = s.dcutoffAngstrom;
		inputs.bcutoffAngstrom = s.bcutoffAngstrom;
		inputs.sigmaMeV = s.sigmaMeV;
		inputs.sigmaMaxMeV = s.sigmaMaxMeV;
		inputs.gammaMeV = s.gammaMeV;
		const auto controls = m_ServerJobControls.find(ActiveCaseId());
		if (controls != m_ServerJobControls.end())
		{
			request.nMin = controls->second.nMin;
			request.nMax = controls->second.nMax;
			request.templatesDirectory = controls->second.templates;
			request.inputs.outputDirectory = controls->second.output;
		}
		if (request.templatesDirectory.Empty())
			request.templatesDirectory = ResolvePythonExampleScript("server_job.py").workingDirectory.Join("install/users/default/scripts/server_templates");
		if (request.inputs.outputDirectory.Empty())
		{
			request.inputs.outputDirectory = DefaultServerJobDirectory();
			if (!m_ProjectDirectory.Empty())
			{
				const auto fallback = m_ProjectDirectory.Join("defectstudio") / request.inputs.outputDirectory.filename();
				if (fallback != request.inputs.outputDirectory) request.fallbackOutputDirectory = fallback;
			}
		}
		return request;
	}

	std::string PhononPanelSession::ServerJobBlocker() const
	{
		if (m_JobSystem.expired()) return "JobSystem niedostępny.";
		const auto request = ServerJobInputs();
		if (request.inputs.forceConstantDirectory.Empty()) return "Ustaw katalog FC defektu.";
		if (!request.inputs.bulkForceConstantDirectory || request.inputs.bulkForceConstantDirectory->Empty()) return "Ustaw katalog FC bulku.";
		if (request.inputs.groundStateDirectory.Empty() || request.inputs.excitedStateDirectory.Empty()) return "Ustaw katalogi GS i ES.";
		if (request.nMin < 1 || request.nMax < request.nMin) return "Wymagane: 1 <= n_min <= n_max.";
		if (request.inputs.outputDirectory.Empty()) return "Wybierz katalog zadania.";
		if (request.inputs.hrMethod == "forces" && request.inputs.groundAtExcitedDirectory.Empty()) return "Metoda forces wymaga GS w geometrii ES.";
		for (const auto &directory : {request.inputs.forceConstantDirectory, *request.inputs.bulkForceConstantDirectory,
			request.inputs.groundStateDirectory, request.inputs.excitedStateDirectory, request.templatesDirectory})
		{
			std::error_code error;
			if (!std::filesystem::is_directory(directory.Native(), error)) return "Katalog nie istnieje: " + directory.Utf8();
			if (OutputContainsInput(request.inputs.outputDirectory, directory)) return "Katalog zadania nie może zawierać wejść ani szablonów.";
		}
		return {};
	}

	void PhononPanelSession::SubmitServerJob()
	{
		if (IsBusy()) return;
		auto &controls = ServerJobControls();
		controls.message = ServerJobBlocker();
		if (!controls.message.empty()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) return;
		auto request = ServerJobInputs();
		controls.output = request.inputs.outputDirectory;
		controls.result = {};
		m_ServerJobCase = ActiveCaseId();
		m_ServerJob = CreateRef<ServerJob>(std::move(request));
		m_ServerJobId = jobs->Submit(m_ServerJob, JobPriority::Normal);
	}

	void PhononPanelSession::pollServerJob()
	{
		if (!m_ServerJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_ServerJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		auto &controls = m_ServerJobControls[m_ServerJobCase];
		if (snapshot && snapshot->status == JobStatus::Completed && m_ServerJob->GetResult())
		{
			controls.result = *m_ServerJob->GetResult();
			controls.message = "Gotowe. Skopiuj folder na klaster; polecenia są w README.txt.";
		}
		else controls.message = m_ServerJob->GetError() ? m_ServerJob->GetError()->userMessage :
			snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Przygotowanie zadania przerwane.";
		m_ServerJob.reset();
		m_ServerJobId = 0;
		m_ServerJobCase.clear();
	}

	void PhononPanelSession::cancelServerJob()
	{
		if (m_ServerJob) cancelJob(m_ServerJobId);
		m_ServerJob.reset();
		m_ServerJobId = 0;
		m_ServerJobCase.clear();
		m_ServerJobControls.clear();
	}
}
