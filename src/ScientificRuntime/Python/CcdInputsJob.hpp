#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Runs ccd_inputs.py; the panel reads the JSON/error only after the job has finished.
	class CcdInputsJob final : public IJob
	{
	public:
		CcdInputsJob(Path groundStateDirectory, Path excitedStateDirectory, Path outputDirectory, double step);
		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;
		[[nodiscard]] const std::optional<std::string> &GetResult() const noexcept { return m_Result; }
		[[nodiscard]] const std::optional<StructuredError> &GetError() const noexcept { return m_Error; }

	private:
		Path m_GroundStateDirectory;
		Path m_ExcitedStateDirectory;
		Path m_OutputDirectory;
		double m_Step;
		ScriptRunner m_ScriptRunner;
		std::optional<std::string> m_Result;
		std::optional<StructuredError> m_Error;
	};
}
