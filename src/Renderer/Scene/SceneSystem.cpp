#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneSystem.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio::SceneSystem
{
	BindingContext MakePathBindingContext(const RendererWindowState &windowState)
	{
		BindingContext context;
		context.atomPosition = [&windowState](const std::size_t index) -> std::optional<glm::vec3> {
			if (index >= windowState.structure.atoms.size())
				return std::nullopt;
			return windowState.structure.atoms[index].cartesianPosition;
		};
		context.atomRadius = [&windowState](const std::size_t index) -> std::optional<float> {
			if (index >= windowState.structure.atoms.size())
				return std::nullopt;
			return windowState.structure.atoms[index].radius;
		};
		context.objectOrigin = [&windowState](const SceneObjectId id) -> std::optional<glm::vec3> {
			// Planes have stable ids but no ECS mirror; their live center is the origin.
			const std::size_t plane = AnnotationIndex(windowState.scenePlanes, id);
			if (plane < windowState.scenePlanes.size())
				return windowState.scenePlanes[plane].center;
			const entt::entity entity = windowState.sceneRegistry.EntityForObjectId(id);
			if (entity == entt::null || !windowState.sceneRegistry.Registry().all_of<TransformComponent>(entity))
				return std::nullopt;
			return windowState.sceneRegistry.Registry().get<TransformComponent>(entity).position;
		};
		context.isScenePath = [&windowState](const SceneObjectId id) {
			return windowState.paths != nullptr && windowState.paths->Store().Find(id) != nullptr;
		};
		return context;
	}

	void SyncSceneWithStructure(SceneRegistry &scene, const RendererStructureData &structure)
	{
		std::vector<SceneObjectId> atomIds;
		atomIds.reserve(scene.AtomEntities().size());
		for (const entt::entity entity : scene.AtomEntities())
			atomIds.push_back(Entity(entity, &scene).GetComponent<SceneObjectComponent>().id);
		std::unordered_map<std::size_t, SceneObjectId> bondIds;
		for (const entt::entity entity : scene.BondEntities())
		{
			Entity bondEntity(entity, &scene);
			const SceneObjectComponent &object = bondEntity.GetComponent<SceneObjectComponent>();
			bondIds.emplace(object.sourceIndex, object.id);
		}

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
			const SceneObjectId id = index < atomIds.size() ? atomIds[index] : SceneObjectId{};
			Entity entity = scene.CreateObject(SceneObjectKind::Atom, index, atom.element + " " + std::to_string(index), id);
			entity.AddComponent<TransformComponent>(TransformComponent{atom.cartesianPosition});
			entity.AddComponent<AtomComponent>(AtomComponent{index, atom.element, atom.radius, atom.color});
			entity.AddComponent<VisibilityComponent>(VisibilityComponent{atom.visible, atom.renderable});
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

			const auto oldId = bondIds.find(index);
			Entity entity = scene.CreateObject(
				SceneObjectKind::Bond, index, "bond " + std::to_string(index), oldId == bondIds.end() ? SceneObjectId{} : oldId->second);
			BondComponent component;
			component.bondIndex = index;
			component.firstAtomEntity = scene.AtomEntities()[bond.firstAtomIndex];
			component.secondAtomEntity = scene.AtomEntities()[bond.secondAtomIndex];
			component.radius = bond.radius;
			component.gradient = bond.gradient;
			entity.AddComponent<BondComponent>(component);
			entity.AddComponent<VisibilityComponent>(VisibilityComponent{bond.visible, bond.renderable});
			entity.AddComponent<SelectionComponent>();
			scene.BondEntities().push_back(static_cast<entt::entity>(entity));
		}
	}

	void ClearStructureSelection(SceneRegistry &scene, RendererWindowState &windowState)
	{
		entt::registry &registry = scene.Registry();
		for (const entt::entity entity : registry.view<const AtomComponent, SelectionComponent>())
			registry.get<SelectionComponent>(entity).selected = false;
		for (const entt::entity entity : registry.view<const BondComponent, SelectionComponent>())
			registry.get<SelectionComponent>(entity).selected = false;
		windowState.selectedAtomIndices.clear();
		windowState.selectedBondIndices.clear();
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
			windowState.structure.atoms[atomComponent.atomIndex].renderable = visibilityComponent.renderable;
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
			windowState.structure.bonds[bondComponent.bondIndex].renderable = visibilityComponent.renderable;
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
		// Minimum image across the cell: a site saved at x = -1e-7 comes back at x = L once the file
		// is reloaded and wrapped into [0, 1), and must still resolve to the same atom.
		const glm::mat3 &lattice = targetStructure.lattice;
		const bool periodic = targetStructure.periodic && std::abs(glm::determinant(lattice)) > 1e-6f;
		const glm::mat3 inverseLattice = periodic ? glm::inverse(lattice) : glm::mat3(1.0f);
		auto minimumImage = [&](const glm::vec3 &delta) {
			if (!periodic)
				return delta;
			const glm::vec3 fractional = inverseLattice * delta;
			return lattice * (fractional - glm::round(fractional));
		};

		for (const glm::vec3 &position : positions)
		{
			float bestDistanceSquared = toleranceSquared;
			std::size_t bestIndex = targetStructure.atoms.size();
			for (std::size_t index = 0; index < targetStructure.atoms.size(); ++index)
			{
				const glm::vec3 delta = minimumImage(targetStructure.atoms[index].cartesianPosition - position);
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

	bool ResolvePinnedMeasurementPosition(
		const RendererStructureData &structure, const RendererWindowState::PinnedMeasurement &pin,
		glm::vec3 &outAnchor)
	{
		// Same anchor formula as RendererPanel::handlePinnedMeasurementInteraction's resolveAnchor
		// lambda (bond midpoint / angle vertex + worldOffset) - ignores a bond's periodic-image shift
		// like that lambda does, fine for a gizmo pivot/hit-test, not the precise render (see
		// OpenGlRendererBackend::renderLabels, which matches the exact periodic bond image instead).
		if (pin.linkBroken)
		{
			if (pin.frozenAtomPositions.size() != pin.atomIndices.size() ||
				(pin.atomIndices.size() != 2 && pin.atomIndices.size() != 3))
				return false;
			if (pin.atomIndices.size() == 2)
				outAnchor = (pin.frozenAtomPositions[0] + pin.frozenAtomPositions[1]) * 0.5f;
			else
				outAnchor = pin.frozenAtomPositions[1];
			outAnchor += pin.worldOffset;
			return true;
		}
		const bool inRange = std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(),
			[&](const std::size_t index) { return index < structure.atoms.size(); });
		if (!inRange)
			return false;

		if (pin.atomIndices.size() == 2)
		{
			outAnchor = (structure.atoms[pin.atomIndices[0]].cartesianPosition +
				structure.atoms[pin.atomIndices[1]].cartesianPosition) * 0.5f;
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

	void SyncLabelEntities(SceneRegistry &scene, RendererWindowState &windowState)
	{
		EnsureScenePersistKeys(windowState);
		for (const entt::entity entity : scene.LabelEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.LabelEntities().clear();
		for (const entt::entity entity : scene.ArrowEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.ArrowEntities().clear();
		for (const entt::entity entity : scene.FreeLabelEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.FreeLabelEntities().clear();
		for (const entt::entity entity : scene.OrbitalEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.OrbitalEntities().clear();
		for (const entt::entity entity : scene.PathEntities())
			scene.DestroyEntity(Entity(entity, &scene));
		scene.PathEntities().clear();

		scene.LabelEntities().reserve(windowState.pinnedMeasurements.size());
		for (std::size_t index = 0; index < windowState.pinnedMeasurements.size(); ++index)
		{
			RendererWindowState::PinnedMeasurement &pin = windowState.pinnedMeasurements[index];
			if (!pin.id.IsValid())
				pin.id = scene.AllocateObjectId();
			Entity entity = scene.CreateObject(SceneObjectKind::PinnedMeasurement, index, "measurement " + std::to_string(index), pin.id);
			pin.id = entity.GetComponent<SceneObjectComponent>().id;
			glm::vec3 anchor(0.0f);
			(void)ResolvePinnedMeasurementPosition(windowState.structure, pin, anchor);
			entity.AddComponent<TransformComponent>(TransformComponent{anchor});
			entity.AddComponent<LabelComponent>(LabelComponent{index});
			const bool isSelected = std::find(
				windowState.selectedPinnedMeasurements.begin(), windowState.selectedPinnedMeasurements.end(), pin.id) !=
				windowState.selectedPinnedMeasurements.end();
			entity.AddComponent<SelectionComponent>(SelectionComponent{isSelected});
			scene.LabelEntities().push_back(static_cast<entt::entity>(entity));
		}

		scene.FreeLabelEntities().reserve(windowState.freeLabels.size());
		for (std::size_t index = 0; index < windowState.freeLabels.size(); ++index)
		{
			RendererWindowState::FreeLabel &label = windowState.freeLabels[index];
			if (!label.id.IsValid())
				label.id = scene.AllocateObjectId();
			Entity entity = scene.CreateObject(SceneObjectKind::FreeLabel, index, "label " + std::to_string(index), label.id);
			label.id = entity.GetComponent<SceneObjectComponent>().id;
			entity.AddComponent<TransformComponent>(TransformComponent{label.worldPosition});
			entity.AddComponent<SelectionComponent>(SelectionComponent{
				std::find(windowState.selectedFreeLabels.begin(), windowState.selectedFreeLabels.end(), label.id) !=
				windowState.selectedFreeLabels.end()});
			scene.FreeLabelEntities().push_back(static_cast<entt::entity>(entity));
		}

		scene.ArrowEntities().reserve(windowState.sceneArrows.size());
		for (std::size_t index = 0; index < windowState.sceneArrows.size(); ++index)
		{
			RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[index];
			if (!arrow.id.IsValid())
				arrow.id = scene.AllocateObjectId();
			Entity entity = scene.CreateObject(SceneObjectKind::SceneArrow, index, "arrow " + std::to_string(index), arrow.id);
			arrow.id = entity.GetComponent<SceneObjectComponent>().id;
			entity.AddComponent<TransformComponent>(TransformComponent{(arrow.start() + arrow.end()) * 0.5f});
			entity.AddComponent<SelectionComponent>(SelectionComponent{
				std::find(windowState.selectedSceneArrows.begin(), windowState.selectedSceneArrows.end(), arrow.id) !=
				windowState.selectedSceneArrows.end()});
			scene.ArrowEntities().push_back(static_cast<entt::entity>(entity));
		}

		scene.OrbitalEntities().reserve(windowState.sceneOrbitals.size());
		for (std::size_t index = 0; index < windowState.sceneOrbitals.size(); ++index)
		{
			RendererWindowState::SceneOrbital &orbital = windowState.sceneOrbitals[index];
			if (!orbital.id.IsValid())
				orbital.id = scene.AllocateObjectId();
			Entity entity = scene.CreateObject(
				SceneObjectKind::SceneOrbital, index, OrbitalPresetName(orbital.preset), orbital.id);
			orbital.id = entity.GetComponent<SceneObjectComponent>().id;
			entity.AddComponent<TransformComponent>(
				TransformComponent{ResolveSceneOrbitalCenters(orbital, windowState.structure).centroid});
			entity.AddComponent<SelectionComponent>(SelectionComponent{
				std::find(windowState.selectedSceneOrbitals.begin(), windowState.selectedSceneOrbitals.end(), orbital.id) !=
				windowState.selectedSceneOrbitals.end()});
			scene.OrbitalEntities().push_back(static_cast<entt::entity>(entity));
		}

		if (windowState.paths != nullptr)
		{
			const PathStore &paths = windowState.paths->Store();
			const BindingContext bindingContext = MakePathBindingContext(windowState);
			scene.PathEntities().reserve(paths.Size());
			for (std::size_t index = 0; index < paths.Size(); ++index)
			{
				// No unset-id branch like the arrow block has: PathStore::Insert rejects an unset id,
				// so every stored path already carries one allocated by the window's registry.
				const ScenePath *storedPath = paths.At(index);
				if (storedPath == nullptr)
					continue;
				const ResolvedNodes resolved = ResolveNodePositions(*storedPath, bindingContext);
				glm::vec3 position(0.0f);
				for (const glm::vec3 &nodePosition : resolved.positions)
					position += nodePosition;
				if (!resolved.positions.empty())
					position /= static_cast<float>(resolved.positions.size());
				Entity entity = scene.CreateObject(
					SceneObjectKind::ScenePath, index,
					storedPath->name.empty() ? "path " + std::to_string(index) : storedPath->name, storedPath->id);
				entity.AddComponent<TransformComponent>(TransformComponent{position});
				entity.AddComponent<SelectionComponent>(SelectionComponent{false});
				scene.PathEntities().push_back(static_cast<entt::entity>(entity));
			}
		}
	}

	SceneObjectId AppendSceneArrow(RendererWindowState &windowState, RendererWindowState::SceneArrow arrow)
	{
		arrow.id = windowState.sceneRegistry.AllocateObjectId();
		windowState.sceneArrows.push_back(std::move(arrow));
		return windowState.sceneArrows.back().id;
	}

	PathSystem &EnsurePathSystem(RendererWindowState &windowState)
	{
		if (windowState.paths == nullptr)
			windowState.paths = CreateUnique<PathSystem>();
		return *windowState.paths;
	}

	SceneObjectId AppendScenePath(RendererWindowState &windowState, ScenePath path)
	{
		path.id = windowState.sceneRegistry.AllocateObjectId();
		const SceneObjectId id = path.id;
		if (!EnsurePathSystem(windowState).Store().Insert(std::move(path)))
			return SceneObjectId{};
		return id;
	}

	std::vector<std::size_t> ResolveSourceIndices(const SceneRegistry &scene, const std::vector<SceneObjectId> &ids)
	{
		std::vector<std::size_t> result;
		result.reserve(ids.size());
		for (const SceneObjectId id : ids)
		{
			const entt::entity entity = scene.EntityForObjectId(id);
			if (entity == entt::null)
				continue;
			result.push_back(scene.Registry().get<SceneObjectComponent>(entity).sourceIndex);
		}
		return result;
	}

	void ApplySceneArrowAtomBuffer(
		RendererWindowState::SceneArrow &arrow,
		const float startRadius,
		const float endRadius,
		const float radiusBuffer)
	{
		if (radiusBuffer <= 0.0f)
			return;

		const glm::vec3 delta = arrow.end() - arrow.start();
		const float distance = glm::length(delta);
		const float requested = radiusBuffer * (startRadius + endRadius);
		if (distance <= 1e-4f || requested <= 1e-4f)
			return;

		// Keep ten percent of the original direction when the requested gaps overlap rather than
		// trimming through the far end and drawing the arrow backwards.
		const float scale = std::min(1.0f, 0.9f * distance / requested);
		const glm::vec3 direction = delta / distance;
		arrow.start() += direction * (radiusBuffer * startRadius * scale);
		arrow.end() -= direction * (radiusBuffer * endRadius * scale);
	}

	void RefreshAnchoredSceneArrows(RendererWindowState &windowState)
	{
		for (RendererWindowState::SceneArrow &arrow : windowState.sceneArrows)
		{
			if (!arrow.startAnchorAtom.has_value() && !arrow.endAnchorAtom.has_value())
				continue;

			// Anchoring owns only the first and last entries; interior path points are never resized or
			// rebound in 32b-1.
			arrow.start() = ResolveAnchor(arrow.start(), arrow.startAnchorAtom, windowState.structure);
			arrow.end() = ResolveAnchor(arrow.end(), arrow.endAnchorAtom, windowState.structure);
			const float startRadius = arrow.startAnchorAtom.has_value() &&
				*arrow.startAnchorAtom < windowState.structure.atoms.size()
				? windowState.structure.atoms[*arrow.startAnchorAtom].radius
				: 0.0f;
			const float endRadius = arrow.endAnchorAtom.has_value() &&
				*arrow.endAnchorAtom < windowState.structure.atoms.size()
				? windowState.structure.atoms[*arrow.endAnchorAtom].radius
				: 0.0f;
			ApplySceneArrowAtomBuffer(arrow, startRadius, endRadius, arrow.atomBuffer);
		}
	}

	void UpdateLabelTransforms(SceneRegistry &scene, const RendererWindowState &windowState)
	{
		const std::vector<entt::entity> &labelEntities = scene.LabelEntities();
		for (std::size_t index = 0; index < labelEntities.size() && index < windowState.pinnedMeasurements.size(); ++index)
		{
			glm::vec3 anchor(0.0f);
			if (!ResolvePinnedMeasurementPosition(windowState.structure, windowState.pinnedMeasurements[index], anchor))
				continue;
			Entity entity(labelEntities[index], &scene);
			entity.GetComponent<TransformComponent>().position = anchor;
		}
		for (std::size_t index = 0; index < scene.FreeLabelEntities().size() && index < windowState.freeLabels.size(); ++index)
			Entity(scene.FreeLabelEntities()[index], &scene).GetComponent<TransformComponent>().position = windowState.freeLabels[index].worldPosition;
		for (std::size_t index = 0; index < scene.ArrowEntities().size() && index < windowState.sceneArrows.size(); ++index)
			Entity(scene.ArrowEntities()[index], &scene).GetComponent<TransformComponent>().position =
				(windowState.sceneArrows[index].start() + windowState.sceneArrows[index].end()) * 0.5f;
		for (std::size_t index = 0; index < scene.OrbitalEntities().size() && index < windowState.sceneOrbitals.size(); ++index)
			Entity(scene.OrbitalEntities()[index], &scene).GetComponent<TransformComponent>().position =
				ResolveSceneOrbitalCenters(windowState.sceneOrbitals[index], windowState.structure).centroid;
	}

	void SyncLabelSelection(SceneRegistry &scene, const RendererWindowState &windowState)
	{
		const auto sync = [&](const std::vector<entt::entity> &entities, const std::vector<SceneObjectId> &selection) {
			for (const entt::entity handle : entities)
			{
				Entity entity(handle, &scene);
				const SceneObjectId id = entity.GetComponent<SceneObjectComponent>().id;
				entity.GetComponent<SelectionComponent>().selected =
					std::find(selection.begin(), selection.end(), id) != selection.end();
			}
		};
		sync(scene.LabelEntities(), windowState.selectedPinnedMeasurements);
		sync(scene.FreeLabelEntities(), windowState.selectedFreeLabels);
		sync(scene.ArrowEntities(), windowState.selectedSceneArrows);
		sync(scene.OrbitalEntities(), windowState.selectedSceneOrbitals);
	}
} // namespace DefectStudio::SceneSystem
