#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Like CcdInputsJob: publishes the output path/error only after execution has finished.
	class PhononDispersionJob final : public IJob
	{
	public:
		PhononDispersionJob(Path forceConstantDirectory, Path outputDirectory, bool inPlane, int points);
		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;
		[[nodiscard]] const std::optional<Path> &GetResult() const noexcept { return m_Result; }
		[[nodiscard]] const std::optional<StructuredError> &GetError() const noexcept { return m_Error; }

	private:
		Path m_ForceConstantDirectory;
		Path m_OutputDirectory;
		bool m_InPlane;
		int m_Points;
		ScriptRunner m_ScriptRunner;
		std::optional<Path> m_Result;
		std::optional<StructuredError> m_Error;
	};
}
