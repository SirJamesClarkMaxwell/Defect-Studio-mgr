#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/VaspOrbitalGridBridge.hpp"

namespace DefectStudio
{
	// Loads one orbital's real-space wavefunction grid via VaspOrbitalGridBridge off the main
	// thread - same convention as OpenDefectJob/VaspOutputJob: result available via GetResult()
	// once JobCompletedEvent fires for this job's id. On failure Execute() throws, producing
	// JobFailedEvent instead. Grid extraction reads the whole WAVECAR file, so this can take
	// several seconds on real calculations - that's the whole reason it's a job, not a direct call.
	class VaspOrbitalGridJob final : public IJob
	{
	public:
		VaspOrbitalGridJob(Path calculationDirectory, int spinChannel, int kpointIndex, int bandIndex);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const Path &GetCalculationDirectory() const noexcept;
		[[nodiscard]] const std::optional<VaspOrbitalGridData> &GetResult() const noexcept;

	private:
		Path m_CalculationDirectory;
		int m_SpinChannel;
		int m_KpointIndex;
		int m_BandIndex;
		VaspOrbitalGridBridge m_Bridge;
		std::optional<VaspOrbitalGridData> m_Result;
	};
} // namespace DefectStudio
