#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/Crystal/StructureComparison.hpp"
#include "ScientificRuntime/Python/PuntukasBridge.hpp"
#include "ScientificRuntime/Python/ScipyAssignmentBridge.hpp"

namespace DefectStudio
{
	// Orchestrates the atoms-displacement comparison off the main thread: loads the comparison
	// structure via PuntukasBridge, builds the cross-structure cost matrix (Domain, pure), solves
	// the optimal assignment via scipy (subprocess), then assembles the final result (Domain,
	// pure). On success the result is available via GetResult() once JobCompletedEvent fires for
	// this job's id (Execute() has fully returned by then, same contract as OpenDefectJob - no
	// synchronization needed to read it back on the main thread). On failure Execute() throws,
	// producing JobFailedEvent instead.
	class CompareStructuresJob final : public IJob
	{
	public:
		CompareStructuresJob(
			CrystalStructure referenceStructure,
			Path comparisonFilePath,
			ElementPropertiesTable elementPropertiesTable);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const Path &GetComparisonFilePath() const noexcept;
		[[nodiscard]] const std::optional<StructureComparisonResult> &GetResult() const noexcept;

	private:
		CrystalStructure m_ReferenceStructure;
		Path m_ComparisonFilePath;
		ElementPropertiesTable m_ElementPropertiesTable;
		PuntukasBridge m_StructureBridge;
		ScipyAssignmentBridge m_AssignmentBridge;
		std::optional<StructureComparisonResult> m_Result;
	};
} // namespace DefectStudio
