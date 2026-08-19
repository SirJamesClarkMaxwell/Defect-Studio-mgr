#include "Core/dspch.hpp"

#include "Renderer/Scene/ViewModifier.hpp"

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	void HideSelectionModifier::Apply(SceneRegistry &scene, RendererWindowState &windowState) const
	{
		entt::registry &registry = scene.Registry();
		for (const entt::entity entity : registry.view<SelectionComponent>())
			if (registry.get<SelectionComponent>(entity).selected)
				registry.get<VisibilityComponent>(entity).visible = false;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);
	}

	void ShowAllModifier::Apply(SceneRegistry &scene, RendererWindowState &windowState) const
	{
		entt::registry &registry = scene.Registry();
		for (const entt::entity entity : registry.view<VisibilityComponent>())
			registry.get<VisibilityComponent>(entity).visible = true;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);
	}

	void InvertSelectionModifier::Apply(SceneRegistry &scene, RendererWindowState &windowState) const
	{
		entt::registry &registry = scene.Registry();
		for (const entt::entity entity : registry.view<SelectionComponent, const VisibilityComponent>())
		{
			const bool visible = registry.get<const VisibilityComponent>(entity).visible;
			SelectionComponent &selection = registry.get<SelectionComponent>(entity);
			selection.selected = visible && !selection.selected;
		}
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);
	}
} // namespace DefectStudio
