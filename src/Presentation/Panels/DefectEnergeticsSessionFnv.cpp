#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsSession.hpp"

#include <algorithm>
#include <format>

#include "Core/JobSystem/JobSystem.hpp"
#include "Presentation/Panels/FnvCorrectionInputs.hpp"
#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string ChargeText(int q)
		{
			return q > 0 ? std::format("+{}", q) : std::to_string(q);
		}
	}
	void DefectEnergeticsSession::startFnvBatch()
	{
		if (m_FnvJob || !m_StoreFnv)
			return;
		if (m_ModelDirty)
			rebuild();
		if (!m_Model.bulk)
			return;
		const Path bulkLocpot = m_File->runs[*m_Model.bulk].directory / Path::FromUtf8("LOCPOT");
		const auto bulkStructure = ReadLocpotStructure(bulkLocpot);
		std::vector<std::string> skipped;
		for (const auto &row : m_Model.rows)
		{
			if (!row.included || row.charge == 0 || row.correction || row.locpot.Empty())
				continue;
			const auto defectStructure = ReadLocpotStructure(row.locpot);
			const auto centre = defectStructure && bulkStructure ? FindImpurityCentre(*defectStructure, *bulkStructure)
				: std::nullopt;
			if (!centre)
			{
				skipped.push_back(std::format("{} q={}", row.defect, ChargeText(row.charge)));
				continue;
			}
			FnvTask task;
			task.defect = row.defect;
			task.charge = row.charge;
			task.entry.defectLocpot = row.locpot;
			task.entry.bulkLocpot = bulkLocpot;
			task.entry.charge = row.charge;
			task.entry.dielectric = m_File->dielectric;
			task.entry.dielectricSource = m_File->dielectricSource;
			task.entry.centreFractional = centre->fractional;
			task.entry.convention = FnvModelConvention::Sxdefectalign;
			m_FnvQueue.push_back(std::move(task));
		}
		m_FnvDone = 0;
		m_FnvTotal = m_FnvQueue.size();
		m_Message = skipped.empty() ? std::string() : "Bez środka (brak domieszki - ustaw w FNV): " + [&] {
			std::string list;
			for (const auto &name : skipped)
				list += (list.empty() ? "" : ", ") + name;
			return list;
		}();
		submitNextFnv();
	}

	void DefectEnergeticsSession::submitNextFnv()
	{
		const auto jobSystem = m_JobSystem.lock();
		if (!jobSystem || m_FnvQueue.empty())
			return;
		m_FnvRunning = std::move(m_FnvQueue.front());
		m_FnvQueue.pop_front();
		FnvProfileRequest request;
		request.defectLocpot = m_FnvRunning->entry.defectLocpot;
		request.bulkLocpot = m_FnvRunning->entry.bulkLocpot;
		request.charge = m_FnvRunning->entry.charge;
		request.dielectric = m_FnvRunning->entry.dielectric;
		request.centreFractional = m_FnvRunning->entry.centreFractional;
		request.energyCutoff = m_FnvRunning->entry.energyCutoff;
		m_FnvJob = CreateRef<FnvCorrectionJob>(request);
		m_FnvJobId = jobSystem->Submit(m_FnvJob, JobPriority::Normal);
	}

	void DefectEnergeticsSession::pollFnvBatch()
	{
		if (!m_FnvJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_FnvJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_FnvJob->GetResult() && m_FnvRunning)
		{
			PersistedFnvCorrection entry = std::move(m_FnvRunning->entry);
			entry.profiles = std::move(*m_FnvJob->GetResult());
			for (int a = 0; a < 3; ++a)
				entry.windows[a] = ProposeFnvAlignment(entry.profiles->axes[a],
					FnvModelPotential(*entry.profiles, a, entry.convention)).window;
			if (m_StoreFnv)
				m_StoreFnv(std::move(entry));
		}
		else if (m_FnvRunning)
			m_Message = std::format("FNV {} q={}: {}", m_FnvRunning->defect, ChargeText(m_FnvRunning->charge),
				snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "nie powiodło się");
		++m_FnvDone;
		m_FnvJob.reset();
		m_FnvJobId = 0;
		m_FnvRunning.reset();
		submitNextFnv();
	}
} // namespace DefectStudio
