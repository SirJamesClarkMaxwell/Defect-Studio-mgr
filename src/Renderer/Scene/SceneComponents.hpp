#pragma once

// Entity/Scene wrapper shape ported from TheCherno/Hazel (github.com/TheCherno/Hazel),
// Apache License 2.0 - structural pattern only (component list, Entity/Scene split, templated
// Add/Get/Has/RemoveComponent), reimplemented from scratch against our own entt::registry. No
// Hazel source files copied into this repo.

#include <cstddef>
#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	struct TransformComponent
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 scale = glm::vec3(1.0f);
	};

	// Back-reference into RendererStructureData::atoms[atomIndex] - the ECS side owns
	// selection/visibility, the flat array stays the render hot path (see SceneSystem).
	struct AtomComponent
	{
		std::size_t atomIndex = 0;
		std::string element;
		float radius = 0.35f;
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
	};

	struct BondComponent
	{
		std::size_t bondIndex = 0;
		entt::entity firstAtomEntity = entt::null;
		entt::entity secondAtomEntity = entt::null;
		float radius = 0.09f;
		RendererColorGradient gradient;
	};

	struct VisibilityComponent
	{
		bool visible = true;
	};

	struct SelectionComponent
	{
		bool selected = false;
	};

	// Structural placeholder - no Collections UI consumes this yet.
	struct CollectionComponent
	{
		std::string collectionId = "default";
	};
} // namespace DefectStudio
