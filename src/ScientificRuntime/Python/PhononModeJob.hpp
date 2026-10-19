#pragma once

#include <optional>
#include <string>
#include <glm/glm.hpp>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Like CcdInputsJob: publishes the output path/error only after execution has finished.
	class PhononModeJob final : public IJob
	{
	public:
		PhononModeJob(Path forceConstantDirectory, Path outputDirectory, glm::dvec3 q, int band, std::string supercell, bool inPlane);
		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;
		[[nodiscard]] const std::optional<Path> &GetResult() const noexcept { return m_Result; }
		[[nodiscard]] const std::optional<StructuredError> &GetError() const noexcept { return m_Error; }

	private:
		Path m_ForceConstantDirectory;
		Path m_OutputDirectory;
		bool m_InPlane;
		glm::dvec3 m_Q;
		int m_Band;
		std::string m_Supercell;
		ScriptRunner m_ScriptRunner;
		std::optional<Path> m_Result;
		std::optional<StructuredError> m_Error;
	};
}
