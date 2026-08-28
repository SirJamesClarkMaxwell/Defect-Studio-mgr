#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/Crystal/StructureComparison.hpp"
#include "ScientificRuntime/Python/ScipyAssignmentBridge.hpp"

namespace DefectStudio
{
	// SceneOutlinerPanel's "Copy view + visibility to..." (RMB on a window row) - matches two
	// already-open windows' atoms (both structures already in memory, unlike CompareStructuresJob
	// which loads its comparison structure off-disk) so the caller can copy which atoms are hidden
	// from the source window onto the corresponding atoms in the target window, atom-index-matched
	// rather than assuming the two structures share the same atom ordering. On success the result is
	// available via GetResult() once JobCompletedEvent fires (Execute() has fully returned by then,
	// same contract as OpenDefectJob/CompareStructuresJob). On failure Execute() throws.
	class CopyWindowStateJob final : public IJob
	{
	public:
		CopyWindowStateJob(
			CrystalStructure sourceStructure,
			CrystalStructure targetStructure,
			ElementPropertiesTable elementPropertiesTable);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const std::optional<StructureComparisonResult> &GetResult() const noexcept;

	private:
		CrystalStructure m_SourceStructure;
		CrystalStructure m_TargetStructure;
		ElementPropertiesTable m_ElementPropertiesTable;
		ScipyAssignmentBridge m_AssignmentBridge;
		std::optional<StructureComparisonResult> m_Result;
	};
} // namespace DefectStudio
