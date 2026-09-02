#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/SuggestSupercellMatrixJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	SuggestSupercellMatrixJob::SuggestSupercellMatrixJob(CrystalStructure unitCell, MillerIndices hkl, int layers)
		: m_UnitCell(std::move(unitCell)),
		  m_Hkl(hkl),
		  m_Layers(layers)
	{
	}

	std::string SuggestSupercellMatrixJob::GetName() const
	{
		return "Surface Supercell: " + (m_UnitCell.name.empty() ? std::string("(unnamed)") : m_UnitCell.name);
	}

	std::string SuggestSupercellMatrixJob::GetType() const
	{
		return "SuggestSupercellMatrixJob";
	}

	void SuggestSupercellMatrixJob::Execute(JobContext &context)
	{
		context.SetStage("surface-matrix");
		context.SetMessage("Deriving surface-oriented supercell matrix via ASE");
		context.SetProgress(0.0f, 1.0f);

		Result<SupercellMatrix> result = m_Bridge.SuggestSurfaceOrientedMatrix(m_UnitCell, m_Hkl, m_Layers);
		if (!result)
			throw std::runtime_error(result.Error().technicalDetails);

		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Surface matrix ready");
	}

	const std::optional<SupercellMatrix> &SuggestSupercellMatrixJob::GetResult() const noexcept
	{
		return m_Result;
	}
} // namespace DefectStudio
