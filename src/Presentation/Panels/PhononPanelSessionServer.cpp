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
		const auto &settings = Settings();
		if (!std::isfinite(settings.sigmaMeV) || settings.sigmaMeV <= 0 ||
			!std::isfinite(settings.gammaMeV) || settings.gammaMeV <= 0)
		{
			m_Error = "sigma i gamma muszą być dodatnie.";
			return;
		}
		m_BatchDone = m_BatchTotal = 0;
		m_BatchCancelling = false;
		m_ServerRuns.clear();
		m_CaseRuns[ActiveCaseId()] = {PhononCaseStatus::Queued, {}, {}};
		for (std::size_t i = 0; i < m_ServerResults.size(); ++i)
		{
			if (i >= m_ServerChecked.size() || !m_ServerChecked[i]) continue;
			const auto &hit = m_ServerResults[i];
			PhononAnalysisRequest request;
			request.serverHrDirectory = hit.hrDirectory;
			request.sigmaMeV = settings.sigmaMeV;
			request.gammaMeV = settings.gammaMeV;
			request.zplFormula = settings.zplFormula;
			request.outputDirectory = serverOutputPath(hit);
			// A project nested in the server system must never turn a read-only import into a write there.
			if (OutputContainsInput(hit.systemDirectory, request.outputDirectory) ||
				OutputContainsInput(request.outputDirectory, hit.systemDirectory))
			{
				m_Error = "Katalog projektu musi być poza drzewem wyników serwera.";
				m_BatchQueue.clear();
				m_ServerRuns.clear();
				m_CaseRuns[ActiveCaseId()] = {PhononCaseStatus::Failed, m_Error, {}};
				m_BatchDone = m_BatchTotal = 0;
				return;
			}
			m_ServerRuns[hit.hrDirectory.Utf8()] = {PhononCaseStatus::Queued, {}, request.outputDirectory};
			m_BatchQueue.push_back({ActiveCaseId(), std::move(request)});
			++m_BatchTotal;
		}
		submitNextBatch();
	}
}
