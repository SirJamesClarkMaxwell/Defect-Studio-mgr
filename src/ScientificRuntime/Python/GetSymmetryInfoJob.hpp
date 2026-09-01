#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio
{
	// Runs SupercellBridge::GetSymmetryInfo (spglib, subprocess) off the main thread - same
	// Submit->poll->GetResult contract as CompareStructuresJob: on success the result is available
	// via GetResult() once JobCompletedEvent fires for this job's id (Execute() has fully returned
	// by then, no synchronization needed to read it back on the main thread); on failure Execute()
	// throws, producing JobFailedEvent instead. Used by NewStructureWizardPanel's optional "Pokaz
	// symetrie" sanity-check button - the only place in that panel a subprocess is involved.
	class GetSymmetryInfoJob final : public IJob
	{
	public:
		GetSymmetryInfoJob(CrystalStructure structure, float symprecAngstrom);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const std::optional<SymmetryInfo> &GetResult() const noexcept;

	private:
		CrystalStructure m_Structure;
		float m_SymprecAngstrom;
		SupercellBridge m_Bridge;
		std::optional<SymmetryInfo> m_Result;
	};
} // namespace DefectStudio
