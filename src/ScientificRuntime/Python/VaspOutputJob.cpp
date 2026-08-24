#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspOutputJob.hpp"

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	VaspOutputJob::VaspOutputJob(
		Path calculationDirectory, int bandStart, int bandEnd, bool includeOrbitals, bool includeIrreps,
		float irrepTol, float symprec)
		: m_CalculationDirectory(std::move(calculationDirectory)), m_BandStart(bandStart), m_BandEnd(bandEnd),
		  m_IncludeOrbitals(includeOrbitals), m_IncludeIrreps(includeIrreps), m_IrrepTol(irrepTol),
		  m_Symprec(symprec) {}

	std::string VaspOutputJob::GetName() const { return "Load VASP Output: " + m_CalculationDirectory.String(); }
	std::string VaspOutputJob::GetType() const { return "VaspOutputJob"; }

	void VaspOutputJob::Execute(JobContext &context)
	{
		context.SetStage("vasp-output");
		context.SetMessage("Loading band-gap/orbital data via puntukas");
		context.SetProgress(0.0f, 1.0f);

		Result<VaspOutputData> loadResult = m_Bridge.LoadOutput(
			m_CalculationDirectory, m_BandStart, m_BandEnd, m_IncludeOrbitals, m_IncludeIrreps, m_IrrepTol,
			m_Symprec);
		if (!loadResult)
			throw std::runtime_error(loadResult.Error().technicalDetails);

		m_Result = std::move(loadResult).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("VASP output loaded");
	}

	const Path &VaspOutputJob::GetCalculationDirectory() const noexcept { return m_CalculationDirectory; }
	const std::optional<VaspOutputData> &VaspOutputJob::GetResult() const noexcept { return m_Result; }
} // namespace DefectStudio
