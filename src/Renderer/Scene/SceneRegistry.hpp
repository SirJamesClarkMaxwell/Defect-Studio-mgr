#pragma once

// Entity/Scene wrapper shape ported from TheCherno/Hazel (github.com/TheCherno/Hazel),
// Apache License 2.0 - structural pattern only, reimplemented against our own entt::registry.
// No Hazel source files copied into this repo.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "Renderer/Scene/Entity.hpp"
#include "Renderer/Scene/SceneObject.hpp"

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

		// Out-of-line since task 20: destroying an entity must also drop its SceneObjectId from the
		// lookup below, so a destroyed id never resolves again (and is never handed out again -
		// m_NextObjectId only ever counts up).
		void DestroyEntity(Entity entity);

		// --- Stable object identity ---

		// Next unused id for this window. Call at the point an object is *created* (a push_back into
		// sceneArrows/freeLabels/pinnedMeasurements), not at sync time, so the object is addressable
		// before the next sync runs.
		[[nodiscard]] SceneObjectId AllocateObjectId();

		// Creates an entity carrying a SceneObjectComponent and records it in the id lookup. Pass an
		// existing id to keep an object's identity across a resync; pass {} to allocate a fresh one.
		// An explicit id advances the allocator past it; an id that is already alive is replaced by a
		// fresh one, so read the final id from the returned entity's SceneObjectComponent.
		Entity CreateObject(SceneObjectKind kind, std::size_t sourceIndex, std::string displayName, SceneObjectId id = {});

		// Invalid Entity if the id was never issued or its object has been destroyed.
		[[nodiscard]] Entity FindObject(SceneObjectId id);

		[[nodiscard]] bool IsAlive(SceneObjectId id) const;

		// entt::null if the id was never issued or its object is gone. The const half of FindObject,
		// for read-only consumers (SceneSystem::ResolveSourceIndices).
		[[nodiscard]] entt::entity EntityForObjectId(SceneObjectId id) const;

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

		// Index -> entity lookup for pinned measurement labels, same shape as AtomEntities/BondEntities
		// above but populated by SceneSystem::SyncLabelEntities from
		// RendererWindowState::pinnedMeasurements instead of RendererStructureData.
		[[nodiscard]] std::vector<entt::entity> &LabelEntities()
		{
			return m_LabelEntities;
		}

		[[nodiscard]] const std::vector<entt::entity> &LabelEntities() const
		{
			return m_LabelEntities;
		}

		[[nodiscard]] Entity LabelEntityAt(std::size_t index)
		{
			if (index >= m_LabelEntities.size())
				return Entity{};
			return Entity(m_LabelEntities[index], this);
		}

		// Index -> entity lookup for scene arrows and free labels, same shape as the three above.
		// Populated by SceneSystem::SyncLabelEntities, which mirrors all three annotation kinds since
		// task 20 - before it, arrows and free labels had no entity at all.
		[[nodiscard]] std::vector<entt::entity> &ArrowEntities()
		{
			return m_ArrowEntities;
		}

		[[nodiscard]] const std::vector<entt::entity> &ArrowEntities() const
		{
			return m_ArrowEntities;
		}

		[[nodiscard]] std::vector<entt::entity> &FreeLabelEntities()
		{
			return m_FreeLabelEntities;
		}

		[[nodiscard]] const std::vector<entt::entity> &FreeLabelEntities() const
		{
			return m_FreeLabelEntities;
		}

		[[nodiscard]] Entity ArrowEntityAt(std::size_t index)
		{
			if (index >= m_ArrowEntities.size())
				return Entity{};
			return Entity(m_ArrowEntities[index], this);
		}

		[[nodiscard]] Entity FreeLabelEntityAt(std::size_t index)
		{
			if (index >= m_FreeLabelEntities.size())
				return Entity{};
			return Entity(m_FreeLabelEntities[index], this);
		}

	private:
		entt::registry m_Registry;
		std::vector<entt::entity> m_AtomEntities;
		std::vector<entt::entity> m_BondEntities;
		std::vector<entt::entity> m_LabelEntities;
		std::vector<entt::entity> m_ArrowEntities;
		std::vector<entt::entity> m_FreeLabelEntities;
		std::unordered_map<std::uint64_t, entt::entity> m_ObjectEntities;
		std::uint64_t m_NextObjectId = 1;
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
