#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"

#include <algorithm>
#include <unordered_map>

#include "Core/Utils/Uuid.hpp"

namespace DefectStudio
{
bool AtomReferenceBinds(const RendererStructureData &structure, const PersistedAtomRef &reference, float tolerance)
{
	if (reference.index >= structure.atoms.size() || structure.atoms[reference.index].element != reference.element)
		return false;
	const glm::vec3 delta = structure.atoms[reference.index].cartesianPosition - reference.position;
	return glm::dot(delta, delta) <= tolerance * tolerance;
}

std::optional<std::size_t> ResolveAtomReference(
	const RendererStructureData &structure, const PersistedAtomRef &reference, float tolerance)
{
	if (AtomReferenceBinds(structure, reference, tolerance))
		return reference.index;

	const float toleranceSquared = tolerance * tolerance;
	std::optional<std::size_t> nearest;
	float nearestDistanceSquared = toleranceSquared;
	for (std::size_t index = 0; index < structure.atoms.size(); ++index)
	{
		if (structure.atoms[index].element != reference.element)
			continue;
		const glm::vec3 delta = structure.atoms[index].cartesianPosition - reference.position;
		const float distanceSquared = glm::dot(delta, delta);
		if (distanceSquared <= nearestDistanceSquared)
		{
			nearest = index;
			nearestDistanceSquared = distanceSquared;
		}
	}
	return nearest;
}

std::string GenerateScenePersistKey()
{
	std::string key = ToString(GenerateUuid());
	key.erase(std::remove(key.begin(), key.end(), '-'), key.end());
	return key;
}

void EnsureScenePersistKeys(RendererWindowState &window)
{
	for (auto &pin : window.pinnedMeasurements)
		if (pin.persistKey.empty())
			pin.persistKey = GenerateScenePersistKey();
	for (auto &label : window.freeLabels)
		if (label.persistKey.empty())
			label.persistKey = GenerateScenePersistKey();
	for (auto &arrow : window.sceneArrows)
		if (arrow.persistKey.empty())
			arrow.persistKey = GenerateScenePersistKey();
	for (auto &orbital : window.sceneOrbitals)
		if (orbital.persistKey.empty())
			orbital.persistKey = GenerateScenePersistKey();
	for (auto &plane : window.scenePlanes)
		if (plane.persistKey.empty())
			plane.persistKey = GenerateScenePersistKey();
	if (window.paths != nullptr)
	{
		// The store only hands out mutable access through the two revision-bumping mutators, so a
		// key assignment costs one spurious style revision. It happens once per path, the first time
		// it is saved, and the alternative is a fresh key on every save - an identity nothing can
		// reference across a reload.
		std::vector<SceneObjectId> keyless;
		window.paths->Store().Visit([&](const ScenePath &path) {
			if (path.persistKey.empty())
				keyless.push_back(path.id);
		});
		for (const SceneObjectId id : keyless)
			window.paths->Store().MutateStyle(id, [](ScenePath &path) { path.persistKey = GenerateScenePersistKey(); });
	}
}

std::vector<PersistedSceneObject> MergeWindowSceneObjects(const std::vector<std::vector<PersistedSceneObject>> &windows)
{
	std::vector<PersistedSceneObject> merged;
	std::unordered_map<std::string, std::size_t> positions;
	for (const auto &window : windows)
		for (const auto &object : window)
		{
			const std::string key = std::visit([](const auto &v) { return v.persistKey; }, object);
			if (key.empty())
			{
				merged.push_back(object);
				continue;
			}
			auto it = positions.find(key);
			if (it == positions.end())
			{
				positions.emplace(key, merged.size());
				merged.push_back(object);
			}
			else
				merged[it->second] = object;
		}
	return merged;
}
} // namespace DefectStudio
