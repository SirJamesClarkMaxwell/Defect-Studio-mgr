#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneOrbitalAim.hpp"

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	std::optional<glm::vec3> AimSceneOrbitalEuler(
		const RendererWindowState::SceneOrbital &orbital, const glm::vec3 &centre, const glm::vec3 &target)
	{
		if (IsTwoCenterPreset(orbital.preset))
			return std::nullopt;
		const auto axis = OrbitalPresetMemberAxis(orbital.preset, orbital.lobeIndex);
		const glm::vec3 offset = target - centre;
		const float distanceSquared = glm::dot(offset, offset);
		if (!axis || !std::isfinite(distanceSquared) || distanceSquared < 1e-8f ||
			!std::isfinite(orbital.rotationEuler.x) || !std::isfinite(orbital.rotationEuler.y) ||
			!std::isfinite(orbital.rotationEuler.z))
			return std::nullopt;
		const glm::quat current = glm::quat(glm::radians(orbital.rotationEuler));
		const glm::quat aim = glm::rotation(glm::normalize(current * *axis), glm::normalize(offset));
		return glm::degrees(glm::eulerAngles(glm::normalize(aim * current)));
	}

	std::vector<OrbitalAimTarget> CollectOrbitalAimTargets(const RendererWindowState &windowState)
	{
		std::vector<OrbitalAimTarget> targets;
		for (std::size_t i = 0; i < windowState.structure.vacancies.size(); ++i)
		{
			const auto &vacancy = windowState.structure.vacancies[i];
			targets.push_back({vacancy.label + " #" + std::to_string(i + 1), vacancy.cartesianPosition});
		}
		for (const std::size_t index : windowState.selectedAtomIndices)
		{
			if (index >= windowState.structure.atoms.size())
				continue;
			const auto &atom = windowState.structure.atoms[index];
			targets.push_back({atom.element + " #" + std::to_string(index + 1), atom.cartesianPosition});
		}
		targets.push_back({"Kursor 3D", windowState.cursor3DPosition});
		return targets;
	}

	std::optional<glm::vec3> ResolveDanglingBondTarget(
		const RendererWindowState &windowState, const std::vector<std::size_t> &atoms)
	{
		glm::vec3 centroid(0.0f);
		std::size_t count = 0;
		for (const std::size_t index : atoms)
		{
			if (index >= windowState.structure.atoms.size())
				continue;
			centroid += windowState.structure.atoms[index].cartesianPosition;
			++count;
		}
		if (count == 0)
			return std::nullopt;
		centroid /= static_cast<float>(count);
		const auto &vacancies = windowState.structure.vacancies;
		if (vacancies.empty())
			return centroid;
		const auto nearest = std::min_element(vacancies.begin(), vacancies.end(), [&](const auto &a, const auto &b) {
			const glm::vec3 da = a.cartesianPosition - centroid;
			const glm::vec3 db = b.cartesianPosition - centroid;
			return glm::dot(da, da) < glm::dot(db, db);
		});
		return nearest->cartesianPosition;
	}

	std::vector<RendererWindowState::SceneOrbital> MakeDanglingBondOrbitals(
		const RendererWindowState &windowState, const std::vector<std::size_t> &atoms, const glm::vec3 &target)
	{
		std::vector<RendererWindowState::SceneOrbital> orbitals;
		for (const std::size_t index : atoms)
		{
			if (index >= windowState.structure.atoms.size())
				continue;
			auto orbital = MakeDefaultSceneOrbital(
				windowState, OrbitalPreset::Sp3, windowState.structure.atoms[index].cartesianPosition, {index});
			orbital.lobeIndex = 0;
			if (const auto euler = AimSceneOrbitalEuler(
					orbital, ResolveSceneOrbitalCenters(orbital, windowState.structure).centerA, target))
				orbital.rotationEuler = *euler;
			orbitals.push_back(std::move(orbital));
		}
		return orbitals;
	}
} // namespace DefectStudio
