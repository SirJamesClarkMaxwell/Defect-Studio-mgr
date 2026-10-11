#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/VaspDensityGridBridge.hpp"

namespace DefectStudio
{
	// VaspDensityGridBridge off the main thread, same convention as VaspOrbitalGridJob: the result is
	// in GetResult() once the job reports Completed; on failure Execute() throws and the job fails
	// with the bridge's technical details as its message.
	class VaspDensityGridJob final : public IJob
	{
	public:
		VaspDensityGridJob(Path chgcarPath, DensityComponent component, Path referencePath);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] std::optional<DensityGrid> &GetResult() noexcept;

	private:
		Path m_ChgcarPath;
		DensityComponent m_Component;
		Path m_ReferencePath;
		VaspDensityGridBridge m_Bridge;
		std::optional<DensityGrid> m_Result;
	};
} // namespace DefectStudio
