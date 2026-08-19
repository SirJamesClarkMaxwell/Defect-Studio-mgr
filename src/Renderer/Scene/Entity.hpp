#pragma once

// Entity/Scene wrapper shape ported from TheCherno/Hazel (github.com/TheCherno/Hazel),
// Apache License 2.0 - structural pattern only, reimplemented against our own entt::registry.
// No Hazel source files copied into this repo.

#include <entt/entt.hpp>

namespace DefectStudio
{
	class SceneRegistry;

	// Thin handle over an entt::entity + the registry that owns it. Copyable (both members are
	// non-owning), invalid (operator bool() == false) once the underlying entity is destroyed.
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, SceneRegistry *scene) : m_Handle(handle), m_Scene(scene)
		{
		}

		template <typename T, typename... Args>
		T &AddComponent(Args &&...args);

		template <typename T>
		[[nodiscard]] T &GetComponent();

		template <typename T>
		[[nodiscard]] bool HasComponent() const;

		template <typename T>
		void RemoveComponent();

		[[nodiscard]] operator bool() const
		{
			return m_Handle != entt::null && m_Scene != nullptr;
		}

		[[nodiscard]] operator entt::entity() const
		{
			return m_Handle;
		}

		[[nodiscard]] bool operator==(const Entity &other) const
		{
			return m_Handle == other.m_Handle && m_Scene == other.m_Scene;
		}

		[[nodiscard]] bool operator!=(const Entity &other) const
		{
			return !(*this == other);
		}

	private:
		entt::entity m_Handle = entt::null;
		SceneRegistry *m_Scene = nullptr;
	};
} // namespace DefectStudio
