#include "Core/dspch.hpp"

#include "Renderer/Scene/HiddenSceneState.hpp"

#include <algorithm>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"

namespace DefectStudio
{
	HiddenSceneState CaptureHiddenSceneState(const RendererStructureData &structure)
	{
		HiddenSceneState state;
		for (std::size_t atomIndex = 0; atomIndex < structure.atoms.size(); ++atomIndex)
			if (!structure.atoms[atomIndex].visible)
				state.atomIndices.push_back(atomIndex);

		for (const RendererBondData &bond : structure.bonds)
		{
			if (bond.visible)
				continue;

			state.bondEndpoints.emplace_back(
				std::min<std::size_t>(bond.firstAtomIndex, bond.secondAtomIndex),
				std::max<std::size_t>(bond.firstAtomIndex, bond.secondAtomIndex));
		}
		return state;
	}

	HiddenSceneState CaptureHiddenSceneState(const RendererWindowState &window)
	{
		const entt::registry &registry = window.sceneRegistry.Registry();
		auto atoms = registry.view<const AtomComponent, const VisibilityComponent>();
		if (atoms.begin() == atoms.end())
			return CaptureHiddenSceneState(window.structure);

		HiddenSceneState state;
		for (const entt::entity entity : atoms)
			if (!atoms.get<const VisibilityComponent>(entity).visible)
				state.atomIndices.push_back(atoms.get<const AtomComponent>(entity).atomIndex);
		std::sort(state.atomIndices.begin(), state.atomIndices.end());

		std::vector<std::size_t> hiddenBonds;
		auto bonds = registry.view<const BondComponent, const VisibilityComponent>();
		for (const entt::entity entity : bonds)
			if (!bonds.get<const VisibilityComponent>(entity).visible)
				hiddenBonds.push_back(bonds.get<const BondComponent>(entity).bondIndex);
		std::sort(hiddenBonds.begin(), hiddenBonds.end());
		for (const std::size_t bondIndex : hiddenBonds)
		{
			if (bondIndex >= window.structure.bonds.size())
				continue;
			const RendererBondData &bond = window.structure.bonds[bondIndex];
			state.bondEndpoints.emplace_back(
				std::min<std::size_t>(bond.firstAtomIndex, bond.secondAtomIndex),
				std::max<std::size_t>(bond.firstAtomIndex, bond.secondAtomIndex));
		}
		return state;
	}

	HiddenSceneState RemapHiddenSceneStateAfterAtomRemoval(
		const HiddenSceneState &state, const std::vector<std::size_t> &removedAtomIndices)
	{
		std::vector<std::size_t> removed = removedAtomIndices;
		std::sort(removed.begin(), removed.end());
		removed.erase(std::unique(removed.begin(), removed.end()), removed.end());

		HiddenSceneState remapped;
		for (const std::size_t atomIndex : state.atomIndices)
		{
			const auto removedAt = std::lower_bound(removed.begin(), removed.end(), atomIndex);
			if (removedAt != removed.end() && *removedAt == atomIndex)
				continue;
			remapped.atomIndices.push_back(atomIndex - static_cast<std::size_t>(removedAt - removed.begin()));
		}

		for (const auto &[firstAtomIndex, secondAtomIndex] : state.bondEndpoints)
		{
			const auto firstRemovedAt = std::lower_bound(removed.begin(), removed.end(), firstAtomIndex);
			if (firstRemovedAt != removed.end() && *firstRemovedAt == firstAtomIndex)
				continue;
			const auto secondRemovedAt = std::lower_bound(removed.begin(), removed.end(), secondAtomIndex);
			if (secondRemovedAt != removed.end() && *secondRemovedAt == secondAtomIndex)
				continue;

			const std::size_t remappedFirst =
				firstAtomIndex - static_cast<std::size_t>(firstRemovedAt - removed.begin());
			const std::size_t remappedSecond =
				secondAtomIndex - static_cast<std::size_t>(secondRemovedAt - removed.begin());
			remapped.bondEndpoints.emplace_back(std::min(remappedFirst, remappedSecond), std::max(remappedFirst, remappedSecond));
		}
		return remapped;
	}

	std::vector<std::size_t> ResolveHiddenBondIndices(
		const RendererStructureData &structure,
		const std::vector<std::pair<std::size_t, std::size_t>> &bondEndpoints)
	{
		std::vector<std::size_t> hiddenBondIndices;
		for (std::size_t bondIndex = 0; bondIndex < structure.bonds.size(); ++bondIndex)
		{
			const RendererBondData &bond = structure.bonds[bondIndex];
			const std::pair<std::size_t, std::size_t> endpoints{
				std::min<std::size_t>(bond.firstAtomIndex, bond.secondAtomIndex),
				std::max<std::size_t>(bond.firstAtomIndex, bond.secondAtomIndex)};
			if (std::find(bondEndpoints.begin(), bondEndpoints.end(), endpoints) != bondEndpoints.end())
				hiddenBondIndices.push_back(bondIndex);
		}
		return hiddenBondIndices;
	}
} // namespace DefectStudio
