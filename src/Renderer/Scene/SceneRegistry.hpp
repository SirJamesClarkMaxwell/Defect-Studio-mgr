#pragma once

// Entity/Scene wrapper shape ported from TheCherno/Hazel (github.com/TheCherno/Hazel),
// Apache License 2.0 - structural pattern only, reimplemented against our own entt::registry.
// No Hazel source files copied into this repo.

#include <cstddef>
#include <vector>

#include <entt/entt.hpp>

#include "Renderer/Scene/Entity.hpp"

namespace DefectStudio
{
	// Owns the entt::registry for one renderer window's atoms/bonds. Move-only (matches
	// entt::registry's own restriction, and RendererWindowState - which will own one of these -
	// is already move-only via its Unique<RendererViewCamera> member, so nothing new breaks).
	class SceneRegistry
	{
	public:
		SceneRegistry() = default;
		SceneRegistry(const SceneRegistry &) = delete;
		SceneRegistry &operator=(const SceneRegistry &) = delete;
		SceneRegistry(SceneRegistry &&) = default;
		SceneRegistry &operator=(SceneRegistry &&) = default;

		[[nodiscard]] Entity CreateEntity()
		{
			return Entity(m_Registry.create(), this);
		}

		void DestroyEntity(Entity entity)
		{
			if (entity)
				m_Registry.destroy(static_cast<entt::entity>(entity));
		}

		[[nodiscard]] entt::registry &Registry()
		{
			return m_Registry;
		}

		[[nodiscard]] const entt::registry &Registry() const
		{
			return m_Registry;
		}

		// Index -> entity lookup, populated by SceneSystem::SyncSceneWithStructure and kept in
		// lockstep with RendererStructureData::atoms/bonds - the bridge between ECS entities and
		// the flat, index-based arrays the GPU instanced-rendering hot path actually reads.
		[[nodiscard]] std::vector<entt::entity> &AtomEntities()
		{
			return m_AtomEntities;
		}

		[[nodiscard]] const std::vector<entt::entity> &AtomEntities() const
		{
			return m_AtomEntities;
		}

		[[nodiscard]] std::vector<entt::entity> &BondEntities()
		{
			return m_BondEntities;
		}

		[[nodiscard]] const std::vector<entt::entity> &BondEntities() const
		{
			return m_BondEntities;
		}

		[[nodiscard]] Entity AtomEntityAt(std::size_t index)
		{
			if (index >= m_AtomEntities.size())
				return Entity{};
			return Entity(m_AtomEntities[index], this);
		}

		[[nodiscard]] Entity BondEntityAt(std::size_t index)
		{
			if (index >= m_BondEntities.size())
				return Entity{};
			return Entity(m_BondEntities[index], this);
		}

	private:
		entt::registry m_Registry;
		std::vector<entt::entity> m_AtomEntities;
		std::vector<entt::entity> m_BondEntities;
	};

	template <typename T, typename... Args>
	T &Entity::AddComponent(Args &&...args)
	{
		return m_Scene->Registry().emplace_or_replace<T>(m_Handle, std::forward<Args>(args)...);
	}

	template <typename T>
	T &Entity::GetComponent()
	{
		return m_Scene->Registry().get<T>(m_Handle);
	}

	template <typename T>
	bool Entity::HasComponent() const
	{
		return m_Scene->Registry().all_of<T>(m_Handle);
	}

	template <typename T>
	void Entity::RemoveComponent()
	{
		m_Scene->Registry().remove<T>(m_Handle);
	}
} // namespace DefectStudio
