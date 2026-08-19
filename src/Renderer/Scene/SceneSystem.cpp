#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneSystem.hpp"

#include <unordered_set>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"

namespace DefectStudio::SceneSystem
{
	void SyncSceneWithStructure(SceneRegistry &scene, const RendererStructureData &structure)
	{
		for (const entt::entity entity : scene.AtomEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		for (const entt::entity entity : scene.BondEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.AtomEntities().clear();
		scene.BondEntities().clear();

		scene.AtomEntities().reserve(structure.atoms.size());
		for (std::size_t index = 0; index < structure.atoms.size(); ++index)
		{
			const RendererAtomData &atom = structure.atoms[index];
			Entity entity = scene.CreateEntity();
			entity.AddComponent<TransformComponent>(TransformComponent{atom.cartesianPosition});
			entity.AddComponent<AtomComponent>(AtomComponent{index, atom.element, atom.radius, atom.color});
			entity.AddComponent<VisibilityComponent>(VisibilityComponent{atom.visible});
			entity.AddComponent<SelectionComponent>();
			entity.AddComponent<CollectionComponent>();
			scene.AtomEntities().push_back(static_cast<entt::entity>(entity));
		}

		scene.BondEntities().reserve(structure.bonds.size());
		for (std::size_t index = 0; index < structure.bonds.size(); ++index)
		{
			const RendererBondData &bond = structure.bonds[index];
			if (bond.firstAtomIndex >= scene.AtomEntities().size() || bond.secondAtomIndex >= scene.AtomEntities().size())
				continue;

			Entity entity = scene.CreateEntity();
			BondComponent component;
			component.bondIndex = index;
			component.firstAtomEntity = scene.AtomEntities()[bond.firstAtomIndex];
			component.secondAtomEntity = scene.AtomEntities()[bond.secondAtomIndex];
			component.radius = bond.radius;
			component.gradient = bond.gradient;
			entity.AddComponent<BondComponent>(component);
			scene.BondEntities().push_back(static_cast<entt::entity>(entity));
		}
	}

	void PushSelectionAndVisibilityToWindowState(const SceneRegistry &scene, RendererWindowState &windowState)
	{
		windowState.selectedAtomIndices.clear();

		const entt::registry &registry = scene.Registry();
		auto view = registry.view<const AtomComponent, const SelectionComponent, const VisibilityComponent>();
		for (const entt::entity entity : view)
		{
			const AtomComponent &atomComponent = view.get<const AtomComponent>(entity);
			const SelectionComponent &selectionComponent = view.get<const SelectionComponent>(entity);
			const VisibilityComponent &visibilityComponent = view.get<const VisibilityComponent>(entity);
			if (atomComponent.atomIndex >= windowState.structure.atoms.size())
				continue;

			windowState.structure.atoms[atomComponent.atomIndex].visible = visibilityComponent.visible;
			if (selectionComponent.selected)
				windowState.selectedAtomIndices.push_back(atomComponent.atomIndex);
		}
	}

	void ApplySelectionAndVisibilityToScene(
		SceneRegistry &scene,
		const std::vector<std::size_t> &selectedAtomIndices,
		const std::vector<std::size_t> &hiddenAtomIndices)
	{
		const std::unordered_set<std::size_t> selectedSet(selectedAtomIndices.begin(), selectedAtomIndices.end());
		const std::unordered_set<std::size_t> hiddenSet(hiddenAtomIndices.begin(), hiddenAtomIndices.end());

		const std::vector<entt::entity> &atomEntities = scene.AtomEntities();
		for (std::size_t index = 0; index < atomEntities.size(); ++index)
		{
			Entity entity(atomEntities[index], &scene);
			entity.GetComponent<SelectionComponent>().selected = selectedSet.contains(index);
			entity.GetComponent<VisibilityComponent>().visible = !hiddenSet.contains(index);
		}
	}
} // namespace DefectStudio::SceneSystem
