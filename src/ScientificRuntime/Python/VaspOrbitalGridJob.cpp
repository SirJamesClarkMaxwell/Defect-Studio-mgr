#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspOrbitalGridJob.hpp"

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	VaspOrbitalGridJob::VaspOrbitalGridJob(
		Path calculationDirectory, int spinChannel, int kpointIndex, int bandIndex)
		: m_CalculationDirectory(std::move(calculationDirectory)),
		  m_SpinChannel(spinChannel), m_KpointIndex(kpointIndex), m_BandIndex(bandIndex) {}

	std::string VaspOrbitalGridJob::GetName() const
	{
		return "Load Orbital Grid: " + m_CalculationDirectory.String() + " band " + std::to_string(m_BandIndex);
	}
	std::string VaspOrbitalGridJob::GetType() const { return "VaspOrbitalGridJob"; }

	void VaspOrbitalGridJob::Execute(JobContext &context)
	{
		context.SetStage("vasp-orbital-grid");
		context.SetMessage("Extracting orbital grid from WAVECAR via puntukas");
		context.SetProgress(0.0f, 1.0f);

		Result<VaspOrbitalGridData> loadResult =
			m_Bridge.LoadOrbitalGrid(m_CalculationDirectory, m_SpinChannel, m_KpointIndex, m_BandIndex);
		if (!loadResult)
			throw std::runtime_error(loadResult.Error().technicalDetails);

		m_Result = std::move(loadResult).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Orbital grid loaded");
	}

	const Path &VaspOrbitalGridJob::GetCalculationDirectory() const noexcept { return m_CalculationDirectory; }
	const std::optional<VaspOrbitalGridData> &VaspOrbitalGridJob::GetResult() const noexcept { return m_Result; }
} // namespace DefectStudio
