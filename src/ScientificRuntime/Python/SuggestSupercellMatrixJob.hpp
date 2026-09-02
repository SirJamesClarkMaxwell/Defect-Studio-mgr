#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio
{
	// Runs SupercellBridge::SuggestSurfaceOrientedMatrix (ASE, subprocess) off the main thread -
	// same Submit->poll->GetResult contract as GetSymmetryInfoJob. Used by SupercellBuilderPanel's
	// "Surface" tab, the only place in that panel a subprocess is involved (every other input mode
	// is plain integer arithmetic).
	class SuggestSupercellMatrixJob final : public IJob
	{
	public:
		SuggestSupercellMatrixJob(CrystalStructure unitCell, MillerIndices hkl, int layers);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const std::optional<SupercellMatrix> &GetResult() const noexcept;

	private:
		CrystalStructure m_UnitCell;
		MillerIndices m_Hkl;
		int m_Layers;
		SupercellBridge m_Bridge;
		std::optional<SupercellMatrix> m_Result;
	};
} // namespace DefectStudio
