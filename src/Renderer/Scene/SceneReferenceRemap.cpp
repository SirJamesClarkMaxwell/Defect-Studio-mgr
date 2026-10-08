#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneReferenceRemap.hpp"

#include <algorithm>
#include <limits>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/VacancyBond.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransformPathElements.hpp"

namespace DefectStudio
{
	namespace
	{
		std::optional<std::size_t> Remap(std::size_t index, const std::vector<std::size_t> &removed)
		{
			const auto at = std::lower_bound(removed.begin(), removed.end(), index);
			if (at != removed.end() && *at == index) return std::nullopt;
			return index - static_cast<std::size_t>(at - removed.begin());
		}

		void RemapList(std::vector<std::size_t> &indices, const std::vector<std::size_t> &removed)
		{
			std::erase_if(indices, [&](std::size_t index) { return !Remap(index, removed); });
			for (auto &index : indices) index = *Remap(index, removed);
		}

		void BakePathTransform(ScenePath &path, const ResolvedNodes &resolved)
		{
			for (std::size_t n = 0; n < path.nodes.size(); ++n) path.nodes[n].position = resolved.positions[n];
			for (std::size_t s = 0; s < path.segments.size(); ++s)
				if (std::holds_alternative<CubicBezierSegmentData>(path.segments[s].data))
				{
					auto &curve = std::get<CubicBezierSegmentData>(path.segments[s].data);
					curve.startHandle.offset = resolved.handlePositions[2 * s] - resolved.positions[s];
					curve.endHandle.offset = resolved.handlePositions[2 * s + 1] - resolved.positions[s + 1];
				}
			path.transform = {};
			path.transformBinding.value = PathTransformBinding::Free{};
		}

		void RemapPaths(RendererWindowState &window, const std::vector<std::size_t> &removed,
			const std::vector<std::size_t> &vacancyIndices, bool atoms)
		{
			if (!window.paths) return;
			const auto context = SceneSystem::MakePathBindingContext(window);
			for (const auto id : window.paths->Store().Ids())
			{
				const auto &original = *window.paths->Store().Find(id);
				const auto generated = GeneratedVacancyBondPair(original);
				if (!atoms && generated && (!Remap(generated->second, removed) ||
					(generated->vacancyPair && !Remap(generated->first, removed))))
				{
					window.paths->ErasePath(id);
					std::erase(window.selectedScenePaths, id);
					std::erase(window.defectFrameChildren.paths, id);
					continue;
				}
				const auto resolved = ResolveNodePositions(original, context);
				const auto transform = ResolvePathTransform(original, context);
				bool convertedAtom = false;
				window.paths->Store().MutateGeometry(id, [&](ScenePath &path) {
					bool baked = false;
					if (atoms && std::holds_alternative<PathTransformBinding::BondFrame>(path.transformBinding.value))
					{
						auto &frame = std::get<PathTransformBinding::BondFrame>(path.transformBinding.value);
						const auto a = Remap(frame.atomA, removed), b = Remap(frame.atomB, removed);
						if (a && b) { frame.atomA = *a; frame.atomB = *b; }
						else
						{
							path.transform = transform;
							path.transformBinding.value = PathTransformBinding::Free{};
							if (!HasInvertibleScenePathTransform(path))
							{
								BakePathTransform(path, resolved);
								baked = true;
							}
							else
								for (std::size_t s = 0; s < path.segments.size(); ++s)
									if (std::holds_alternative<CubicBezierSegmentData>(path.segments[s].data))
									{
										auto &curve = std::get<CubicBezierSegmentData>(path.segments[s].data);
										curve.startHandle.offset = ScenePathWorldToLocal(path, resolved.handlePositions[2 * s]) -
											ScenePathWorldToLocal(path, resolved.positions[s]);
										curve.endHandle.offset = ScenePathWorldToLocal(path, resolved.handlePositions[2 * s + 1]) -
											ScenePathWorldToLocal(path, resolved.positions[s + 1]);
									}
						}
					}
					for (std::size_t i = 0; i < path.nodes.size(); ++i)
					{
						auto &node = path.nodes[i];
						bool freeze = false;
						if (atoms && std::holds_alternative<PathBinding::CopyPosition>(node.binding.value))
						{
							auto &binding = std::get<PathBinding::CopyPosition>(node.binding.value);
							if (const auto next = Remap(binding.atomIndex, removed)) binding.atomIndex = *next;
							else
							{
								convertedAtom = true;
								const auto at = std::lower_bound(removed.begin(), removed.end(), binding.atomIndex);
								node.binding.value = PathBinding::CopyVacancy{
									vacancyIndices[at - removed.begin()], binding.offset, 0.0f};
								if (generated)
									for (auto &end : path.nodes)
										if (std::holds_alternative<PathBinding::CopyVacancy>(end.binding.value))
											std::get<PathBinding::CopyVacancy>(end.binding.value).buffer = 0.0f;
							}
						}
						else if (atoms && std::holds_alternative<PathBinding::BondMidpoint>(node.binding.value))
						{
							auto &binding = std::get<PathBinding::BondMidpoint>(node.binding.value);
							const auto a = Remap(binding.atomA, removed), b = Remap(binding.atomB, removed);
							if (a && b) { binding.atomA = *a; binding.atomB = *b; }
							else freeze = true;
						}
						else if (!atoms && std::holds_alternative<PathBinding::CopyVacancy>(node.binding.value))
						{
							auto &binding = std::get<PathBinding::CopyVacancy>(node.binding.value);
							if (const auto next = Remap(binding.vacancyIndex, removed)) binding.vacancyIndex = *next;
							else freeze = true;
						}
						if (freeze)
						{
							ScenePath frame;
							frame.transform = transform;
							// A collapsed transform cannot hold a frozen world point: bake the path once.
							if (!baked && !HasInvertibleScenePathTransform(frame))
							{
								BakePathTransform(path, resolved);
								baked = true;
							}
							node.position = baked ? resolved.positions[i] : ScenePathWorldToLocal(frame, resolved.positions[i]);
							node.binding.value = PathBinding::Free{};
						}
					}
				});
				const auto &path = *window.paths->Store().Find(id);
				if (convertedAtom && path.nodes.size() == 2 &&
					std::holds_alternative<PathBinding::CopyVacancy>(path.nodes.front().binding.value) &&
					std::holds_alternative<PathBinding::CopyVacancy>(path.nodes.back().binding.value) &&
					std::get<PathBinding::CopyVacancy>(path.nodes.front().binding.value).vacancyIndex ==
					std::get<PathBinding::CopyVacancy>(path.nodes.back().binding.value).vacancyIndex)
				{
					window.paths->ErasePath(id);
					std::erase(window.selectedScenePaths, id);
					std::erase(window.defectFrameChildren.paths, id);
				}
			}
		}
	}

	void RemapSceneAtomReferencesAfterAtomRemoval(RendererWindowState &window,
		const std::vector<std::size_t> &removed, const std::vector<std::size_t> &vacancyIndices)
	{
		if (removed.empty()) return;
		RemapPaths(window, removed, vacancyIndices, true);
		for (auto &pin : window.pinnedMeasurements)
		{
			if (pin.linkBroken) continue;
			if (std::any_of(pin.atomIndices.begin(), pin.atomIndices.end(), [&](auto index) { return !Remap(index, removed); }))
			{
				pin.linkBroken = true;
				pin.frozenAtomPositions.clear();
				pin.frozenAtomElements.clear();
				for (auto index : pin.atomIndices)
				{
					const bool valid = index < window.structure.atoms.size();
					pin.frozenAtomPositions.push_back(valid ? window.structure.atoms[index].cartesianPosition : glm::vec3(0.0f));
					pin.frozenAtomElements.push_back(valid ? window.structure.atoms[index].element : std::string{});
				}
			}
			else RemapList(pin.atomIndices, removed);
		}
		RefreshAnchoredFreeLabels(window);
		for (auto &label : window.freeLabels)
			if (label.anchorAtom)
			{
				const auto next = Remap(*label.anchorAtom, removed);
				if (!next)
					label.anchorVacancy = vacancyIndices[std::lower_bound(removed.begin(), removed.end(), *label.anchorAtom) - removed.begin()];
				label.anchorAtom = next;
			}
		ResolveAnchoredOrbitals(window);
		for (auto &orbital : window.sceneOrbitals)
		{
			RemapList(orbital.anchorAtoms, removed);
			for (auto &component : orbital.lcaoComponents)
				if (component.anchorAtom != std::numeric_limits<std::size_t>::max())
					component.anchorAtom = Remap(component.anchorAtom, removed).value_or(std::numeric_limits<std::size_t>::max());
		}
		ResolveAnchoredScenePlanes(window);
		for (auto &plane : window.scenePlanes) RemapList(plane.anchorAtoms, removed);
		for (auto &volume : window.sceneHideVolumes)
		{
			volume.center = ResolveHideVolumeCenter(volume, window.structure).value_or(volume.center);
			RemapList(volume.anchorAtoms, removed);
		}
		if (window.displacementComparison)
		{
			auto &result = window.displacementComparison->result;
			std::erase_if(result.matches, [&](const auto &match) { return !Remap(match.referenceAtomIndex, removed); });
			for (auto &match : result.matches) match.referenceAtomIndex = *Remap(match.referenceAtomIndex, removed);
			RemapList(result.unmatchedReferenceAtomIndices, removed);
		}
	}

	void RemapSceneVacancyReferencesAfterVacancyRemoval(RendererWindowState &window,
		const std::vector<std::size_t> &removed)
	{
		if (removed.empty()) return;
		RemapPaths(window, removed, {}, false);
		RefreshAnchoredFreeLabels(window);
		for (auto &label : window.freeLabels)
			if (label.anchorVacancy) label.anchorVacancy = Remap(*label.anchorVacancy, removed);
		RemapList(window.defectFrameChildren.vacancies, removed);
	}
}
