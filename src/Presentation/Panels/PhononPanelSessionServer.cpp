#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace DefectStudio
{
	Path PhononPanelSession::serverOutputPath(const ServerHrResult &hit) const
	{
		const auto &size = hit.supercell;
		const auto base = m_ProjectDirectory / "defectstudio/phonons/server";
		const auto name = hit.systemDirectory.filename().Utf8();
		const auto tail = std::format("{}x{}x{}", size[0], size[1], size[2]) + "/" + hit.variant;
		for (int suffix = 1; ; ++suffix)
		{
			const auto output = base.Join(suffix == 1 ? name : name + "_" + std::to_string(suffix)).Join(tail);
			const auto queued = std::ranges::find_if(m_BatchQueue, [&](const auto &entry) {
				return std::holds_alternative<PhononAnalysisRequest>(entry.request) &&
					SameFilePath(std::get<PhononAnalysisRequest>(entry.request).outputDirectory, output, m_ProjectDirectory);
			});
			if (queued != m_BatchQueue.end()) continue;
			const auto dataset = ReadDatasetRef(output);
			if (!dataset)
			{
				if (!FileSystem::Exists(output.Native())) return output;
				continue;
			}
			const auto source = dataset->inputs.find("hr_dir");
			std::error_code error;
			if (source != dataset->inputs.end() && (SameFilePath(source->second, hit.hrDirectory, m_ProjectDirectory) ||
				std::filesystem::equivalent(source->second.Native(), hit.hrDirectory.Native(), error))) return output;
		}
	}

	void PhononPanelSession::importServerResults()
	{
		if (IsBusy() || m_ProjectDirectory.Empty() || ActiveCaseId().empty()) return;
		std::vector<ServerHrResult> selected;
		for (std::size_t i = 0; i < m_ServerResults.size(); ++i)
			if (i < m_ServerChecked.size() && m_ServerChecked[i]) selected.push_back(m_ServerResults[i]);
		if (queueServerResults(ActiveCaseId(), selected)) submitNextBatch();
	}

	bool PhononPanelSession::queueServerResults(const std::string &caseId, const std::vector<ServerHrResult> &results)
	{
		if (results.empty()) return true;
		if (m_ProjectDirectory.Empty()) { m_Error = "Otwórz projekt przed importem wyników."; return false; }
		if (m_JobSystem.expired() && !m_CaseJobSubmitter) { m_Error = "JobSystem niedostępny."; return false; }
		const auto &cases = Cases();
		const auto found = std::ranges::find(cases, caseId, &PhononCase::id);
		if (found == cases.end()) return false;
		const auto settings = found->settings;
		if (!std::isfinite(settings.sigmaMeV) || settings.sigmaMeV <= 0 ||
			!std::isfinite(settings.gammaMeV) || settings.gammaMeV <= 0)
		{
			m_Error = "sigma i gamma muszą być dodatnie.";
			return false;
		}
		if (settings.sigmaMaxMeV && (!std::isfinite(*settings.sigmaMaxMeV) || *settings.sigmaMaxMeV < settings.sigmaMeV))
		{
			m_Error = "sigma_max musi być skończona i nie mniejsza od sigma.";
			return false;
		}
		if (!IsBatchRunning())
		{
			m_BatchDone = m_BatchTotal = 0;
			m_BatchCancelling = false;
			m_ServerRuns.clear();
		}
		const auto before = m_BatchQueue.size();
		for (const auto &hit : results)
		{
			PhononAnalysisRequest request;
			request.serverHrDirectory = hit.hrDirectory;
			request.sigmaMeV = settings.sigmaMeV;
			request.gammaMeV = settings.gammaMeV;
			request.sigmaMaxMeV = settings.sigmaMaxMeV;
			request.zplFormula = settings.zplFormula;
			request.outputDirectory = serverOutputPath(hit);
			// A project nested in the server system must never turn a read-only import into a write there.
			if (OutputContainsInput(hit.systemDirectory, request.outputDirectory) ||
				OutputContainsInput(request.outputDirectory, hit.systemDirectory))
			{
				m_Error = "Katalog projektu musi być poza drzewem wyników serwera.";
				m_BatchQueue.resize(before);
				m_CaseRuns[caseId] = {PhononCaseStatus::Failed, m_Error, {}};
				return false;
			}
			m_BatchQueue.push_back({caseId, std::move(request)});
		}
		for (std::size_t i = before; i < m_BatchQueue.size(); ++i)
		{
			const auto &request = std::get<PhononAnalysisRequest>(m_BatchQueue[i].request);
			m_ServerRuns[request.serverHrDirectory.Utf8()] = {PhononCaseStatus::Queued, {}, request.outputDirectory};
		}
		m_BatchTotal += m_BatchQueue.size() - before;
		m_CaseRuns[caseId] = {PhononCaseStatus::Queued, {}, {}};
		return true;
	}
}
