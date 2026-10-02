#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneOrbitalLcao.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Renderer/Scene/SceneOrbitalAim.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	Result<RendererWindowState::SceneOrbital> BuildSalcSceneOrbital(
		const RendererWindowState &windowState, const SymmetryAdaptedVector &vector,
		const SelectionBasis &basis, SalcBasisFunction function, std::string displayName)
	{
		const auto error = [](const char *code, const char *message) {
			return StructuredError{ErrorCategory::Validation, Severity::Error, message, message,
				"Correct the basis or use a real combination of the projected vectors.", "SceneOrbitalLcao", code};
		};
		if (vector.coefficients.size() != basis.sites.size() || vector.coefficients.size() != basis.atomIndices.size())
			return error("orbital.salc.site_count_mismatch", "The coefficients and basis sites do not match.");
		for (const auto &coefficient : vector.coefficients)
			if (std::abs(coefficient.numericImaginary) >= 1e-9 || !std::isfinite(coefficient.numericImaginary))
				return error("orbital.salc.complex_coefficient", "Complex coefficients need a real combination before drawing.");
		for (const std::size_t index : basis.atomIndices)
			if (index >= windowState.structure.atoms.size())
				return error("orbital.salc.atom_out_of_range", "A basis atom no longer exists.");

		const OrbitalPreset preset = function == SalcBasisFunction::S ? OrbitalPreset::S
			: function == SalcBasisFunction::PTowardCentre ? OrbitalPreset::P : OrbitalPreset::Sp3;
		auto orbital = MakeDefaultSceneOrbital(windowState, preset, glm::vec3(basis.centre), {});
		orbital.displayName = std::move(displayName);
		for (std::size_t i = 0; i < basis.sites.size(); ++i)
		{
			const double coefficient = vector.coefficients[i].numeric;
			if (!std::isfinite(coefficient))
				return error("orbital.salc.all_zero", "The orbital contains a non-finite coefficient.");
			if (std::abs(coefficient) < 1e-9)
				continue;
			RendererWindowState::SceneOrbital::LcaoComponent component;
			component.anchorAtom = basis.atomIndices[i];
			const auto &atom = windowState.structure.atoms[component.anchorAtom];
			component.center = atom.cartesianPosition;
			component.preset = preset;
			component.shell = std::max(ValenceShell(atom.element), function == SalcBasisFunction::S ? 1 : 2);
			component.effectiveCharge = ValenceEffectiveCharge(atom.element);
			component.coefficient = static_cast<float>(coefficient);
			if (function != SalcBasisFunction::S)
			{
				RendererWindowState::SceneOrbital aimed;
				aimed.preset = preset;
				component.rotationEuler = AimSceneOrbitalEuler(
					aimed, glm::vec3(basis.sites[i].position), glm::vec3(0.0f)).value_or(glm::vec3(0.0f));
			}
			orbital.lcaoComponents.push_back(component);
		}
		if (orbital.lcaoComponents.empty())
			return error("orbital.salc.all_zero", "All orbital coefficients are zero.");
		return orbital;
	}
} // namespace DefectStudio
