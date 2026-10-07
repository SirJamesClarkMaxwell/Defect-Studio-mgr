#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneSelection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	void ClearAllSceneSelection(RendererWindowState &window)
	{
		SceneSystem::ClearStructureSelection(window.sceneRegistry, window);
		window.selectedPinnedMeasurements.clear();
		window.selectedFreeLabels.clear();
		window.selectedSceneOrbitals.clear();
		window.selectedScenePlanes.clear();
		window.selectedSceneDensities.clear();
		window.selectedScenePaths.clear();
		window.selectedVacancies.clear();
		window.defectFrameSelected = false;
		SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
	}

	void SelectAllVisibleSceneObjects(RendererWindowState &window, bool deselect)
	{
		std::vector<std::size_t> atoms, bonds, vacancies;
		std::vector<SceneObjectId> pins, labels, orbitals, planes, paths;
		if (window.pickAtoms && window.showAtoms)
			for (std::size_t i = 0; i < window.structure.atoms.size(); ++i)
				if (window.structure.atoms[i].visible) atoms.push_back(i);
		if (window.pickBonds && window.showBonds)
			for (std::size_t i = 0; i < window.structure.bonds.size(); ++i)
			{
				const auto &bond = window.structure.bonds[i];
				if (bond.visible && bond.firstAtomIndex < window.structure.atoms.size() &&
					bond.secondAtomIndex < window.structure.atoms.size() &&
					window.structure.atoms[bond.firstAtomIndex].visible &&
					window.structure.atoms[bond.secondAtomIndex].visible) bonds.push_back(i);
			}
		if ((window.pickAtoms || window.pickLabels) && window.showVacancies)
			for (std::size_t i = 0; i < window.structure.vacancies.size(); ++i)
				if (!window.structure.vacancies[i].hidden) vacancies.push_back(i);
		const bool frame = (window.pickAtoms || window.pickLabels) && window.showDefectFrame &&
			window.structure.defectFrame.has_value();
		auto visibleIds = [](const auto &objects, auto &ids) {
			for (const auto &object : objects) if (object.visible) ids.push_back(object.id);
		};
		if (window.pickLabels)
		{
			visibleIds(window.pinnedMeasurements, pins);
			visibleIds(window.freeLabels, labels);
			for (const auto &orbital : window.sceneOrbitals)
				if (orbital.visible && !CachedSceneOrbitalMesh(window, orbital, window.structure).empty())
					orbitals.push_back(orbital.id);
			visibleIds(window.scenePlanes, planes);
		}
		// Paths follow their own Object/Edit Mode picker and remain selectable in every pick mask.
		if (window.paths)
			window.paths->Store().Visit([&](const ScenePath &path) {
				if (path.visible && path.renderable && path.nodes.size() >= 2 && !path.segments.empty())
					paths.push_back(path.id);
			});
		auto containsAll = [](const auto &selection, const auto &ids) {
			return std::all_of(ids.begin(), ids.end(), [&](const auto id) {
				return std::find(selection.begin(), selection.end(), id) != selection.end();
			});
		};
		const bool any = !atoms.empty() || !bonds.empty() || !vacancies.empty() || !pins.empty() ||
			!labels.empty() || !orbitals.empty() || !planes.empty() || !paths.empty() || frame;
		const bool all = any && containsAll(window.selectedAtomIndices, atoms) &&
			containsAll(window.selectedBondIndices, bonds) && containsAll(window.selectedVacancies, vacancies) &&
			containsAll(window.selectedPinnedMeasurements, pins) && containsAll(window.selectedFreeLabels, labels) &&
			containsAll(window.selectedSceneOrbitals, orbitals) && containsAll(window.selectedScenePlanes, planes) &&
			containsAll(window.selectedScenePaths, paths) && (!frame || window.defectFrameSelected);
		if (deselect || all)
		{
			ClearAllSceneSelection(window);
			return;
		}
		auto &scene = window.sceneRegistry;
		if (window.pickAtoms)
			for (const auto entity : scene.Registry().view<AtomComponent, SelectionComponent, const VisibilityComponent>())
				scene.Registry().get<SelectionComponent>(entity).selected =
					window.showAtoms && scene.Registry().get<const VisibilityComponent>(entity).visible;
		if (window.pickBonds)
			for (std::size_t i = 0; i < window.structure.bonds.size(); ++i)
				if (auto entity = scene.BondEntityAt(i))
					entity.GetComponent<SelectionComponent>().selected =
						std::find(bonds.begin(), bonds.end(), i) != bonds.end();
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, window);
		if (window.pickLabels)
		{
			window.selectedPinnedMeasurements = std::move(pins);
			window.selectedFreeLabels = std::move(labels);
			window.selectedSceneOrbitals = std::move(orbitals);
			window.selectedScenePlanes = std::move(planes);
		}
		window.selectedScenePaths = std::move(paths);
		if (window.pickAtoms || window.pickLabels)
		{
			window.selectedVacancies = std::move(vacancies);
			window.defectFrameSelected = frame;
		}
		SceneSystem::SyncLabelSelection(scene, window);
	}

	void ApplySceneDrawingRegionSelection(RendererWindowState &window, glm::vec2 minimum,
		glm::vec2 maximum, float radius, bool replace, bool subtract)
	{
		if (!window.camera) return;
		if (replace)
		{
			window.selectedSceneOrbitals.clear();
			window.selectedScenePlanes.clear();
			window.selectedVacancies.clear();
			window.defectFrameSelected = false;
		}
		const auto vp = window.camera->ProjectionMatrix() * window.camera->ViewMatrix();
		auto project = [&](const glm::vec3 &world) {
			return SelectionHitTest::ProjectToScreen(vp, window.viewportSize, world);
		};
		auto inside = [&](glm::vec2 point) {
			return radius > 0.0f ? SelectionHitTest::PointInCircle(point, minimum, radius) :
				SelectionHitTest::PointInRect(point, minimum, maximum);
		};
		auto triangleHits = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
			const auto x = project(a), y = project(b), z = project(c);
			if (!x || !y || !z) return false;
			const glm::vec2 edge1 = *y - *x, edge2 = *z - *x;
			const bool degenerate = std::abs(edge1.x * edge2.y - edge1.y * edge2.x) < 1e-8f;
			if (radius > 0.0f)
				return (degenerate ? std::min({
					SelectionHitTest::DistancePointToSegment(minimum, *x, *y),
					SelectionHitTest::DistancePointToSegment(minimum, *y, *z),
					SelectionHitTest::DistancePointToSegment(minimum, *z, *x)}) :
					SelectionHitTest::DistancePointToTriangle2D(minimum, *x, *y, *z)) <= radius;
			if (inside(*x) || inside(*y) || inside(*z)) return true;
			const std::array<glm::vec2, 4> corners = {minimum, {maximum.x, minimum.y}, maximum, {minimum.x, maximum.y}};
			if (!degenerate)
				for (const auto corner : corners)
					if (SelectionHitTest::PointInTriangle(corner, *x, *y, *z)) return true;
			// Clip each triangle edge against the rectangle (slab intervals).
			const std::array<glm::vec2, 3> points = {*x, *y, *z};
			for (std::size_t edge = 0; edge < points.size(); ++edge)
			{
				const auto start = points[edge], delta = points[(edge + 1) % points.size()] - start;
				float low = 0.0f, high = 1.0f;
				for (int axis = 0; axis < 2; ++axis)
				{
					if (std::abs(delta[axis]) < 1e-8f)
					{
						if (start[axis] < minimum[axis] || start[axis] > maximum[axis]) high = -1.0f;
						continue;
					}
					const float first = (minimum[axis] - start[axis]) / delta[axis];
					const float second = (maximum[axis] - start[axis]) / delta[axis];
					low = std::max(low, std::min(first, second));
					high = std::min(high, std::max(first, second));
				}
				if (low <= high) return true;
			}
			return false;
		};
		auto apply = [subtract](auto &selection, const auto id) {
			const auto found = std::find(selection.begin(), selection.end(), id);
			if (subtract && found != selection.end()) selection.erase(found);
			else if (!subtract && found == selection.end()) selection.push_back(id);
		};
		if (window.pickLabels)
		{
			for (const auto &orbital : window.sceneOrbitals)
			{
				if (!orbital.visible) continue;
				const auto &mesh = CachedSceneOrbitalMesh(window, orbital, window.structure);
				for (std::size_t i = 0; i + 2 < mesh.size(); i += 3)
					if (triangleHits(mesh[i].position, mesh[i+1].position, mesh[i+2].position))
					{
						apply(window.selectedSceneOrbitals, orbital.id);
						break;
					}
			}
			for (const auto &plane : window.scenePlanes)
			{
				if (!plane.visible) continue;
				const auto corners = ScenePlaneCorners(plane);
				if (triangleHits(corners[0], corners[1], corners[2]) || triangleHits(corners[0], corners[2], corners[3]))
					apply(window.selectedScenePlanes, plane.id);
			}
		}
		if (window.pickAtoms || window.pickLabels)
		{
			if (window.showVacancies)
				for (std::size_t i = 0; i < window.structure.vacancies.size(); ++i)
				{
					const auto &vacancy = window.structure.vacancies[i];
					const auto screen = project(vacancy.cartesianPosition);
					if (!vacancy.hidden && screen && inside(*screen)) apply(window.selectedVacancies, i);
				}
			if (window.showDefectFrame && window.structure.defectFrame)
			{
				const auto screen = project(window.structure.defectFrame->origin);
				if (screen && inside(*screen)) window.defectFrameSelected = !subtract;
			}
		}
		SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
	}
}
