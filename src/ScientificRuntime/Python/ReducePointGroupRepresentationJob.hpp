#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "ScientificRuntime/Python/GroupTheoryBridge.hpp"

namespace DefectStudio
{
	// Runs GroupTheoryBridge::ReduceRepresentation off the main thread - same Submit->poll->GetResult
	// contract as GetSymmetryInfoJob: on success the result is readable via GetResult() once
	// JobCompletedEvent fires for this job's id (Execute() has fully returned by then, so no
	// synchronization is needed to read it back on the main thread); on failure Execute() throws,
	// producing JobFailedEvent instead.
	//
	// This job is the only supported entry point for UI code. There is no UI code yet - the spike's
	// tests call the bridge directly - but nothing on the main thread may call the bridge, because a
	// cold `import groupy` costs ~680 ms and would stall a frame.
	class ReducePointGroupRepresentationJob final : public IJob
	{
	public:
		explicit ReducePointGroupRepresentationJob(PermutationRepresentationRequest request);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const std::optional<PointGroupReduction> &GetResult() const noexcept;

	private:
		PermutationRepresentationRequest m_Request;
		GroupTheoryBridge m_Bridge;
		std::optional<PointGroupReduction> m_Result;
	};
} // namespace DefectStudio
