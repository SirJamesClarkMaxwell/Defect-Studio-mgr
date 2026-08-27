#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneSystem.hpp"

#include <algorithm>
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
			entity.AddComponent<VisibilityComponent>(VisibilityComponent{bond.visible});
			entity.AddComponent<SelectionComponent>();
			scene.BondEntities().push_back(static_cast<entt::entity>(entity));
		}
	}

	void PushSelectionAndVisibilityToWindowState(const SceneRegistry &scene, RendererWindowState &windowState)
	{
		windowState.selectedAtomIndices.clear();
		windowState.selectedBondIndices.clear();

		const entt::registry &registry = scene.Registry();
		auto atomView = registry.view<const AtomComponent, const SelectionComponent, const VisibilityComponent>();
		for (const entt::entity entity : atomView)
		{
			const AtomComponent &atomComponent = atomView.get<const AtomComponent>(entity);
			const SelectionComponent &selectionComponent = atomView.get<const SelectionComponent>(entity);
			const VisibilityComponent &visibilityComponent = atomView.get<const VisibilityComponent>(entity);
			if (atomComponent.atomIndex >= windowState.structure.atoms.size())
				continue;

			windowState.structure.atoms[atomComponent.atomIndex].visible = visibilityComponent.visible;
			if (selectionComponent.selected)
				windowState.selectedAtomIndices.push_back(atomComponent.atomIndex);
		}

		auto bondView = registry.view<const BondComponent, const SelectionComponent, const VisibilityComponent>();
		for (const entt::entity entity : bondView)
		{
			const BondComponent &bondComponent = bondView.get<const BondComponent>(entity);
			const SelectionComponent &selectionComponent = bondView.get<const SelectionComponent>(entity);
			const VisibilityComponent &visibilityComponent = bondView.get<const VisibilityComponent>(entity);
			if (bondComponent.bondIndex >= windowState.structure.bonds.size())
				continue;

			windowState.structure.bonds[bondComponent.bondIndex].visible = visibilityComponent.visible;
			if (selectionComponent.selected)
				windowState.selectedBondIndices.push_back(bondComponent.bondIndex);
		}
	}

	void ApplySelectionAndVisibilityToScene(
		SceneRegistry &scene,
		const std::vector<std::size_t> &selectedAtomIndices,
		const std::vector<std::size_t> &hiddenAtomIndices,
		const std::vector<std::size_t> &selectedBondIndices,
		const std::vector<std::size_t> &hiddenBondIndices)
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

		const std::unordered_set<std::size_t> selectedBondSet(selectedBondIndices.begin(), selectedBondIndices.end());
		const std::unordered_set<std::size_t> hiddenBondSet(hiddenBondIndices.begin(), hiddenBondIndices.end());

		const std::vector<entt::entity> &bondEntities = scene.BondEntities();
		for (std::size_t index = 0; index < bondEntities.size(); ++index)
		{
			Entity entity(bondEntities[index], &scene);
			entity.GetComponent<SelectionComponent>().selected = selectedBondSet.contains(index);
			entity.GetComponent<VisibilityComponent>().visible = !hiddenBondSet.contains(index);
		}
	}

	std::vector<std::size_t> ResolveAtomIndicesByPosition(
		const RendererStructureData &targetStructure,
		const std::vector<glm::vec3> &positions,
		float tolerance)
	{
		std::vector<std::size_t> resolvedIndices;
		resolvedIndices.reserve(positions.size());
		const float toleranceSquared = tolerance * tolerance;

		for (const glm::vec3 &position : positions)
		{
			float bestDistanceSquared = toleranceSquared;
			std::size_t bestIndex = targetStructure.atoms.size();
			for (std::size_t index = 0; index < targetStructure.atoms.size(); ++index)
			{
				const glm::vec3 delta = targetStructure.atoms[index].cartesianPosition - position;
				const float distanceSquared = glm::dot(delta, delta);
				if (distanceSquared <= bestDistanceSquared)
				{
					bestDistanceSquared = distanceSquared;
					bestIndex = index;
				}
			}
			if (bestIndex < targetStructure.atoms.size())
				resolvedIndices.push_back(bestIndex);
		}
		return resolvedIndices;
	}

	namespace
	{
		// Same anchor formula as RendererPanel::handlePinnedMeasurementInteraction's resolveAnchor
		// lambda (bond midpoint / angle vertex + worldOffset) - ignores a bond's periodic-image shift
		// like that lambda does, fine for a gizmo pivot/hit-test, not the precise render (see
		// OpenGlRendererBackend::renderLabels, which matches the exact periodic bond image instead).
		[[nodiscard]] bool ResolveLabelAnchor(
			const RendererStructureData &structure, const RendererWindowState::PinnedMeasurement &pin, glm::vec3 &outAnchor)
		{
			const bool inRange = std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(),
				[&](const std::size_t index) { return index < structure.atoms.size(); });
			if (!inRange)
				return false;

			if (pin.atomIndices.size() == 2)
			{
				outAnchor =
					(structure.atoms[pin.atomIndices[0]].cartesianPosition + structure.atoms[pin.atomIndices[1]].cartesianPosition) *
					0.5f;
			}
			else if (pin.atomIndices.size() == 3)
			{
				const std::size_t vertexIndex = ResolveAngleVertexIndex(structure, pin.atomIndices);
				outAnchor = structure.atoms[vertexIndex].cartesianPosition;
			}
			else
			{
				return false;
			}
			outAnchor += pin.worldOffset;
			return true;
		}
	} // namespace

	void SyncLabelEntities(SceneRegistry &scene, RendererWindowState &windowState)
	{
		for (const entt::entity entity : scene.LabelEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.LabelEntities().clear();

		scene.LabelEntities().reserve(windowState.pinnedMeasurements.size());
		for (std::size_t index = 0; index < windowState.pinnedMeasurements.size(); ++index)
		{
			Entity entity = scene.CreateEntity();
			glm::vec3 anchor(0.0f);
			(void)ResolveLabelAnchor(windowState.structure, windowState.pinnedMeasurements[index], anchor);
			entity.AddComponent<TransformComponent>(TransformComponent{anchor});
			entity.AddComponent<LabelComponent>(LabelComponent{index});
			const bool isSelected = std::find(
				windowState.selectedPinnedMeasurements.begin(), windowState.selectedPinnedMeasurements.end(), index) !=
				windowState.selectedPinnedMeasurements.end();
			entity.AddComponent<SelectionComponent>(SelectionComponent{isSelected});
			scene.LabelEntities().push_back(static_cast<entt::entity>(entity));
		}
	}

	void UpdateLabelTransforms(SceneRegistry &scene, const RendererWindowState &windowState)
	{
		const std::vector<entt::entity> &labelEntities = scene.LabelEntities();
		for (std::size_t index = 0; index < labelEntities.size() && index < windowState.pinnedMeasurements.size(); ++index)
		{
			glm::vec3 anchor(0.0f);
			if (!ResolveLabelAnchor(windowState.structure, windowState.pinnedMeasurements[index], anchor))
				continue;
			Entity entity(labelEntities[index], &scene);
			entity.GetComponent<TransformComponent>().position = anchor;
		}
	}

	void SyncLabelSelection(SceneRegistry &scene, const RendererWindowState &windowState)
	{
		const std::vector<entt::entity> &labelEntities = scene.LabelEntities();
		for (std::size_t index = 0; index < labelEntities.size(); ++index)
		{
			Entity entity(labelEntities[index], &scene);
			entity.GetComponent<SelectionComponent>().selected = std::find(
				windowState.selectedPinnedMeasurements.begin(), windowState.selectedPinnedMeasurements.end(), index) !=
				windowState.selectedPinnedMeasurements.end();
		}
	}
} // namespace DefectStudio::SceneSystem
