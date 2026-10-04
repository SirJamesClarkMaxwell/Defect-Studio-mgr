#include "Core/dspch.hpp"

#include "Renderer/Scene/ScenePathPersistence.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <variant>

#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename Visit>
		void VisitPersistKeys(const RendererWindowState &window, Visit visit)
		{
			const auto annotations = [&](const auto &objects) {
				for (const auto &object : objects)
					visit(object.id, object.persistKey);
			};
			annotations(window.pinnedMeasurements);
			annotations(window.freeLabels);
			annotations(window.sceneArrows);
			annotations(window.sceneOrbitals);
			annotations(window.scenePlanes);
			if (window.paths != nullptr)
				window.paths->Store().Visit([&](const ScenePath &path) { visit(path.id, path.persistKey); });
		}
	}

	PersistedScenePath ExtractPersistedScenePath(const ScenePath &path, const RendererWindowState &window)
	{
		PersistedScenePath saved = ExtractPersistedScenePath(path, window.structure);
		std::unordered_map<SceneObjectId, std::string> keys;
		VisitPersistKeys(window, [&](SceneObjectId id, const std::string &key) { keys.emplace(id, key); });
		for (std::size_t index = 0; index < path.nodes.size(); ++index)
			if (std::holds_alternative<PathBinding::ObjectOrigin>(path.nodes[index].binding.value))
				if (const auto found = keys.find(std::get<PathBinding::ObjectOrigin>(path.nodes[index].binding.value).object); found != keys.end())
					saved.nodes[index].binding.objectPersistKey = found->second;
		return saved;
	}

	void ResolveScenePathObjectOriginBindings(RendererWindowState &window, SceneObjectId pathId,
		const PersistedScenePath &persisted, std::vector<StructuredError> &warnings)
	{
		if (window.paths == nullptr || !window.paths->Store().Contains(pathId) ||
			std::none_of(persisted.nodes.begin(), persisted.nodes.end(),
				[](const PersistedPathNode &node) { return node.binding.kind == "ObjectOrigin"; }))
			return;
		std::unordered_map<std::string, SceneObjectId> ids;
		VisitPersistKeys(window, [&](SceneObjectId id, const std::string &key) {
			if (key.empty())
				return;
			const auto [found, inserted] = ids.emplace(key, id);
			if (!inserted)
				found->second = {}; // An ambiguous key must never silently bind to the wrong object.
		});
		window.paths->Store().MutateGeometry(pathId, [&](ScenePath &path) {
			for (std::size_t index = 0; index < path.nodes.size(); ++index)
			{
				const PersistedPathBinding &binding = persisted.nodes[index].binding;
				if (binding.kind != "ObjectOrigin")
					continue;
				const auto target = ids.find(binding.objectPersistKey);
				if (target != ids.end() && target->second.IsValid() && !window.paths->Store().Contains(target->second) &&
					std::isfinite(binding.offset.x) && std::isfinite(binding.offset.y) && std::isfinite(binding.offset.z))
					path.nodes[index].binding = PathBinding{PathBinding::ObjectOrigin{target->second, binding.offset}};
				else
				{
					path.nodes[index].binding = PathBinding{};
					warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path binding is unresolved",
						"ObjectOrigin target '" + binding.objectPersistKey + "' is missing, ambiguous, names a path, or has an invalid offset.",
						"The node keeps its stored position and is treated as free.", "ScenePathPersistence",
						"scene_objects.path_binding_unresolved");
				}
			}
		});
	}
}
