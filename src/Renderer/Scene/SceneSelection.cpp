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
#include "Renderer/Scene/SceneHideVolume.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"

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
		window.selectedSceneHideVolumes.clear();
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
				if (!window.structure.vacancies[i].hidden && !PointCoveredByHideVolumes(window.sceneHideVolumes,
					window.structure.vacancies[i].cartesianPosition, window.structure)) vacancies.push_back(i);
		const bool frame = (window.pickAtoms || window.pickLabels) && window.showDefectFrame &&
			window.structure.defectFrame.has_value();
		auto visibleIds = [](const auto &objects, auto &ids) {
			for (const auto &object : objects) if (object.visible) ids.push_back(object.id);
		};
		if (window.pickLabels)
		{
			visibleIds(window.pinnedMeasurements, pins);
			for (const auto &label : window.freeLabels)
				if (IsFreeLabelVisible(label, window.structure, window.sceneHideVolumes)) labels.push_back(label.id);
			for (const auto &orbital : window.sceneOrbitals)
				if (orbital.visible && !CachedSceneOrbitalMesh(window, orbital, window.structure).empty())
					orbitals.push_back(orbital.id);
			visibleIds(window.scenePlanes, planes);
		}
		// Paths follow their own Object/Edit Mode picker and remain selectable in every pick mask.
		if (window.paths)
			window.paths->Store().Visit([&](const ScenePath &path) {
				if (path.visible && path.renderable && path.nodes.size() >= 2 && !path.segments.empty() &&
					!BoundToHiddenEndpoint(path, window.structure, window.sceneHideVolumes))
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
		const bool hasActiveVolumes = std::any_of(window.sceneHideVolumes.begin(), window.sceneHideVolumes.end(),
			[](const auto &volume) { return volume.active; });
		auto orbitalTriangleHits = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
			if (!triangleHits(a, b, c)) return false;
			if (!hasActiveVolumes) return true;
			// Each non-inverted shape is convex, so three covered vertices cover the whole face.
			for (const auto &volume : window.sceneHideVolumes)
				if (volume.active && !volume.invert &&
					PointInHideVolume(volume, a, window.structure.lattice, window.structure) &&
					PointInHideVolume(volume, b, window.structure.lattice, window.structure) &&
					PointInHideVolume(volume, c, window.structure.lattice, window.structure)) return false;
			const auto x = project(a), y = project(b), z = project(c);
			if (!x || !y || !z) return false;
			const auto cross = [](glm::vec2 u, glm::vec2 v) { return u.x * v.y - u.y * v.x; };
			const float area = cross(*y - *x, *z - *x);
			if (std::abs(area) < 1e-8f) return false;
			const glm::vec3 inverseW(1.0f / (vp * glm::vec4(a, 1)).w,
				1.0f / (vp * glm::vec4(b, 1)).w, 1.0f / (vp * glm::vec4(c, 1)).w);
			auto visibleAt = [&](glm::vec2 point) {
				if (!inside(point)) return false;
				const float u = cross(*y - point, *z - point) / area;
				const float v = cross(*z - point, *x - point) / area;
				const float w = 1.0f - u - v;
				if (u < 0 || v < 0 || w < 0) return false;
				// Perspective-correct interpolation gives the ray's world-space surface hit.
				const glm::vec3 weights = glm::vec3(u, v, w) * inverseW;
				const glm::vec3 world = (a * weights.x + b * weights.y + c * weights.z) /
					(weights.x + weights.y + weights.z);
				return !PointCoveredByHideVolumes(window.sceneHideVolumes, world, window.structure);
			};
			if (visibleAt(radius > 0 ? minimum : (minimum + maximum) * 0.5f) ||
				visibleAt(*x) || visibleAt(*y) || visibleAt(*z)) return true;
			const glm::vec2 regionMin = radius > 0 ? minimum - glm::vec2(radius) : minimum;
			const glm::vec2 regionMax = radius > 0 ? minimum + glm::vec2(radius) : maximum;
			const glm::vec2 low = glm::max(glm::max(glm::min(*x, glm::min(*y, *z)), regionMin), glm::vec2(0));
			const glm::vec2 high = glm::min(glm::min(glm::max(*x, glm::max(*y, *z)), regionMax), window.viewportSize);
			// ponytail: clipped regions sample pixel centres; use analytic clipping if subpixel hits are needed.
			for (float py = std::ceil(low.y - 0.5f) + 0.5f; py <= high.y; py += 1.0f)
				for (float px = std::ceil(low.x - 0.5f) + 0.5f; px <= high.x; px += 1.0f)
					if (visibleAt(glm::vec2(px, py))) return true;
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
					if (orbitalTriangleHits(mesh[i].position, mesh[i+1].position, mesh[i+2].position))
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
					if (!vacancy.hidden && !PointCoveredByHideVolumes(window.sceneHideVolumes,
						vacancy.cartesianPosition, window.structure) && screen && inside(*screen)) apply(window.selectedVacancies, i);
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
