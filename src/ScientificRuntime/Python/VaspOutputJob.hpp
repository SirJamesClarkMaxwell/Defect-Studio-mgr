#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/VaspOutputBridge.hpp"

namespace DefectStudio
{
	// Loads band-gap/orbital data via VaspOutputBridge off the main thread. Same convention as
	// OpenDefectJob: result available via GetResult() once JobCompletedEvent fires for this job's
	// id (Execute() has fully returned by then, no synchronization needed to read it back). On
	// failure Execute() throws, producing JobFailedEvent instead.
	class VaspOutputJob final : public IJob
	{
	public:
		VaspOutputJob(
			Path calculationDirectory, int bandStart = 0, int bandEnd = 10, bool includeOrbitals = true,
			bool includeIrreps = false, float irrepTol = 0.1f, float symprec = 1e-3f);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const Path &GetCalculationDirectory() const noexcept;
		[[nodiscard]] const std::optional<VaspOutputData> &GetResult() const noexcept;

	private:
		Path m_CalculationDirectory;
		int m_BandStart;
		int m_BandEnd;
		bool m_IncludeOrbitals;
		bool m_IncludeIrreps;
		float m_IrrepTol;
		float m_Symprec;
		VaspOutputBridge m_Bridge;
		std::optional<VaspOutputData> m_Result;
	};
} // namespace DefectStudio
