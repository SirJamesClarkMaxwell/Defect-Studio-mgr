#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <nlohmann/json.hpp>
#include "Core/JobSystem/JobSystem.hpp"
#include "Presentation/Figures/PlotData.hpp"
#include "ScientificRuntime/Python/PythonScriptJob.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	void PhononPanelSession::submitFigure(const Path &specPath, bool all)
	{
		if (m_FigureJobId || m_ProjectDirectory.Empty()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_FigureMessage = "JobSystem niedostępny."; return; }
		try
		{
			const auto script = ResolvePythonExampleScript("render_figure.py");
			const auto arguments = all ? std::vector<std::string>{"--all", m_ProjectDirectory.Join("figures").Utf8()} :
				std::vector<std::string>{specPath.Utf8()};
			m_FigureJobId = jobs->Submit(CreateRef<PythonScriptJob>("Renderowanie rycin", script.scriptPath,
				arguments, script.workingDirectory), JobPriority::Normal);
			m_FigureMessage = all ? "Renderowanie wszystkich rycin…" : "Renderowanie: " + PlotSourceComment(specPath, m_ProjectDirectory);
		}
		catch (const std::exception &error)
		{
			m_FigureMessage = error.what();
		}
	}

	void PhononPanelSession::pollFigureJob()
	{
		if (!m_FigureJobId) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_FigureJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		m_FigureMessage = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Renderowanie rycin przerwane.";
		// PythonScriptJob retains stdout in the existing job log; no second result channel is needed.
		if (snapshot && snapshot->status == JobStatus::Completed)
		{
			try
			{
				std::string json;
				for (const auto &entry : jobs->GetLogs(m_FigureJobId))
					if (entry.message.starts_with("python stdout: ")) json = ExtractJsonLineFromOutput(entry.message.substr(15));
				const auto result = nlohmann::json::parse(json);
				if (!result.at("ok").get<bool>()) m_FigureMessage = result.at("error").get<std::string>();
				else
				{
					m_FigureMessage = "Zapisano:";
					for (const auto &output : result.at("outputs"))
						m_FigureMessage += "\n" + PlotSourceComment(Path::FromUtf8(output.get<std::string>()), m_ProjectDirectory);
					if (result.at("outputs").empty()) m_FigureMessage = "Brak plików figures/*.figure.json.";
				}
			}
			catch (const std::exception &error)
			{
				m_FigureMessage = "Nieprawidłowy wynik renderera: " + std::string(error.what());
			}
		}
		m_FigureJobId = 0;
	}

	void PhononPanelSession::cancelFigureJob()
	{
		if (m_FigureJobId) cancelJob(m_FigureJobId);
		m_FigureJobId = 0;
		m_FigureMessage.clear();
	}
}
