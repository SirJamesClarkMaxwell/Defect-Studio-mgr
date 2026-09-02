#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/GetSymmetryInfoJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	GetSymmetryInfoJob::GetSymmetryInfoJob(CrystalStructure structure, float symprecAngstrom)
		: m_Structure(std::move(structure)),
		  m_SymprecAngstrom(symprecAngstrom)
	{
	}

	std::string GetSymmetryInfoJob::GetName() const
	{
		return "Symmetry Info: " + (m_Structure.name.empty() ? std::string("(unnamed)") : m_Structure.name);
	}

	std::string GetSymmetryInfoJob::GetType() const
	{
		return "GetSymmetryInfoJob";
	}

	void GetSymmetryInfoJob::Execute(JobContext &context)
	{
		context.SetStage("symmetry-info");
		context.SetMessage("Computing spacegroup/point group/Wyckoff positions via spglib");
		context.SetProgress(0.0f, 1.0f);

		Result<SymmetryInfo> result = m_Bridge.GetSymmetryInfo(m_Structure, m_SymprecAngstrom);
		if (!result)
			throw std::runtime_error(result.Error().technicalDetails);

		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Symmetry info complete");
	}

	const std::optional<SymmetryInfo> &GetSymmetryInfoJob::GetResult() const noexcept
	{
		return m_Result;
	}
} // namespace DefectStudio
