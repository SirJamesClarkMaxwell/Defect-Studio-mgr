#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneRegistry.hpp"

#include <utility>

namespace DefectStudio
{
	const char *SceneObjectKindName(const SceneObjectKind kind)
	{
		switch (kind)
		{
		case SceneObjectKind::Atom: return "Atom";
		case SceneObjectKind::Bond: return "Bond";
		case SceneObjectKind::PinnedMeasurement: return "Pinned measurement";
		case SceneObjectKind::FreeLabel: return "Free label";
		case SceneObjectKind::SceneArrow: return "Scene arrow";
		case SceneObjectKind::SceneOrbital: return "Scene orbital";
		}
		return "Object";
	}

	void SceneRegistry::DestroyEntity(const Entity entity)
	{
		if (!entity || !m_Registry.valid(static_cast<entt::entity>(entity)))
			return;

		const entt::entity handle = static_cast<entt::entity>(entity);
		if (m_Registry.all_of<SceneObjectComponent>(handle))
			m_ObjectEntities.erase(m_Registry.get<SceneObjectComponent>(handle).id.value);
		m_Registry.destroy(handle);
	}

	SceneObjectId SceneRegistry::AllocateObjectId()
	{
		return SceneObjectId{m_NextObjectId++};
	}

	Entity SceneRegistry::CreateObject(
		const SceneObjectKind kind, const std::size_t sourceIndex, std::string displayName, SceneObjectId id)
	{
		// A live duplicate (e.g. an annotation copied together with its id) gets a fresh id instead of
		// silently stealing the lookup slot; callers read the final id back from the component.
		if (!id.IsValid() || IsAlive(id))
			id = AllocateObjectId();
		else if (id.value >= m_NextObjectId)
			m_NextObjectId = id.value + 1; // explicit ids (loaded from disk) must never be handed out again

		Entity entity = CreateEntity();
		entity.AddComponent<SceneObjectComponent>(SceneObjectComponent{id, kind, sourceIndex, std::move(displayName)});
		m_ObjectEntities[id.value] = static_cast<entt::entity>(entity);
		return entity;
	}

	Entity SceneRegistry::FindObject(const SceneObjectId id)
	{
		const auto found = m_ObjectEntities.find(id.value);
		if (found == m_ObjectEntities.end() || !m_Registry.valid(found->second))
			return Entity{};
		return Entity(found->second, this);
	}

	bool SceneRegistry::IsAlive(const SceneObjectId id) const
	{
		return EntityForObjectId(id) != entt::null;
	}

	entt::entity SceneRegistry::EntityForObjectId(const SceneObjectId id) const
	{
		const auto found = m_ObjectEntities.find(id.value);
		if (found == m_ObjectEntities.end() || !m_Registry.valid(found->second))
			return entt::null;
		return found->second;
	}
} // namespace DefectStudio
