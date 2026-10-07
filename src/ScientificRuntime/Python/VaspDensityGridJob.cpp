#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspDensityGridJob.hpp"

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	VaspDensityGridJob::VaspDensityGridJob(Path chgcarPath, const DensityComponent component, Path referencePath)
		: m_ChgcarPath(std::move(chgcarPath)), m_Component(component), m_ReferencePath(std::move(referencePath)) {}

	std::string VaspDensityGridJob::GetName() const
	{
		return std::string("Load density (") + DensityComponentKey(m_Component) + "): " + m_ChgcarPath.Utf8();
	}
	std::string VaspDensityGridJob::GetType() const { return "VaspDensityGridJob"; }

	void VaspDensityGridJob::Execute(JobContext &context)
	{
		context.SetStage("vasp-density-grid");
		context.SetMessage("Reading CHGCAR via puntukas");
		context.SetProgress(0.0f, 1.0f);

		Result<DensityGrid> loadResult = m_Bridge.LoadDensityGrid(m_ChgcarPath, m_Component, m_ReferencePath);
		if (!loadResult)
			throw std::runtime_error(loadResult.Error().userMessage + "\n" + loadResult.Error().technicalDetails);

		m_Result = std::move(loadResult).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Density loaded");
	}

	std::optional<DensityGrid> &VaspDensityGridJob::GetResult() noexcept { return m_Result; }
} // namespace DefectStudio
