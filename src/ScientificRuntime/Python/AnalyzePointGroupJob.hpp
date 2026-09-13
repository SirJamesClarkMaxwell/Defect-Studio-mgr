#pragma once

#include <optional>
#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "ScientificRuntime/Python/GroupTheoryBridge.hpp"

namespace DefectStudio
{
	// Runs GroupTheoryBridge::Analyze off the main thread. Same Submit->poll->GetResult contract as
	// GetSymmetryInfoJob, with one difference: on failure the bridge's StructuredError is kept and
	// readable via GetError() after JobStatus::Failed, so the panel can tell "groupy missing" from
	// "basis not closed" by code instead of parsing JobSnapshot::errorMessage text. Execute() still
	// throws (userMessage) so JobSystem reports the failure as usual.
	class AnalyzePointGroupJob final : public IJob
	{
	public:
		explicit AnalyzePointGroupJob(PointGroupAnalysisRequest request);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const std::optional<PointGroupAnalysisResult> &GetResult() const noexcept;
		[[nodiscard]] const std::optional<StructuredError> &GetError() const noexcept;

	private:
		PointGroupAnalysisRequest m_Request;
		GroupTheoryBridge m_Bridge;
		std::optional<PointGroupAnalysisResult> m_Result;
		std::optional<StructuredError> m_Error;
	};
} // namespace DefectStudio
