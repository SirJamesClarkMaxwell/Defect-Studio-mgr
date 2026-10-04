#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/ScenePathPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Path/PathTopology.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>


namespace DefectStudio
{
static PersistedLabelStyle ToPersisted(const RendererWindowState::LabelStyle &s)
{
	PersistedLabelStyle p;
	p.textColor = s.textColor;
	p.textAlpha = s.textAlpha;
	p.backgroundColor = s.backgroundColor;
	p.backgroundAlpha = s.backgroundAlpha;
	p.outlineColor = s.outlineColor;
	p.outlineWidth = s.outlineWidth;
	p.cornerRadius = s.cornerRadius;
	p.padding = s.padding;
	p.strokeColor = s.strokeColor;
	p.strokeWidth = s.strokeWidth;
	p.scale = s.scale;
	return p;
}
static RendererWindowState::LabelStyle FromPersisted(const PersistedLabelStyle &s)
{
	RendererWindowState::LabelStyle p;
	p.textColor = s.textColor;
	p.textAlpha = s.textAlpha;
	p.backgroundColor = s.backgroundColor;
	p.backgroundAlpha = s.backgroundAlpha;
	p.outlineColor = s.outlineColor;
	p.outlineWidth = s.outlineWidth;
	p.cornerRadius = s.cornerRadius;
	p.padding = s.padding;
	p.strokeColor = s.strokeColor;
	p.strokeWidth = s.strokeWidth;
	p.scale = s.scale;
	return p;
}
static std::vector<PersistedAtomRef> PersistAtomReferences(
	const RendererStructureData &structure, const std::vector<std::size_t> &atomIndices)
{
	std::vector<PersistedAtomRef> references;
	references.reserve(atomIndices.size());
	for (const std::size_t atomIndex : atomIndices)
	{
		if (atomIndex >= structure.atoms.size())
			continue;
		PersistedAtomRef reference;
		reference.index = atomIndex;
		reference.element = structure.atoms[atomIndex].element;
		reference.position = structure.atoms[atomIndex].cartesianPosition;
		references.push_back(std::move(reference));
	}
	return references;
}

static std::vector<std::size_t> ResolveAtomReferences(
	const RendererStructureData &structure, const std::vector<PersistedAtomRef> &references)
{
	std::vector<std::size_t> atomIndices;
	atomIndices.reserve(references.size());
	for (const PersistedAtomRef &reference : references)
	{
		const std::optional<std::size_t> resolved = ResolveAtomReference(structure, reference);
		if (!resolved)
			return {};
		atomIndices.push_back(*resolved);
	}
	return atomIndices;
}

static std::vector<PersistedAtomRef> PersistOptionalAtomReference(
	const RendererStructureData &structure, const std::optional<std::size_t> &atomIndex)
{
	const std::vector<std::size_t> indices = atomIndex.has_value()
		? std::vector<std::size_t>{*atomIndex}
		: std::vector<std::size_t>{};
	return PersistAtomReferences(structure, indices);
}

static std::optional<std::size_t> ResolveOptionalAtomReference(
	const RendererStructureData &structure, const std::vector<PersistedAtomRef> &references)
{
	const std::vector<std::size_t> indices = ResolveAtomReferences(structure, references);
	return indices.size() == 1 ? std::optional<std::size_t>(indices.front()) : std::nullopt;
}

std::vector<PersistedSceneObject> ExtractPersistedSceneObjects(const RendererWindowState &window)
{
	std::vector<PersistedSceneObject> result;
	for (const auto &pin : window.pinnedMeasurements)
	{
		PersistedPinnedMeasurement p;
		p.persistKey = pin.persistKey;
		p.linkBroken = pin.linkBroken;
		p.labelOffset = pin.worldOffset;
		p.alignToBondDirection = pin.alignToBondDirection;
		p.flipped = pin.flipped;
		p.rotationOffsetRadians = pin.rotationOffsetRadians;
		p.bondPeriodicOffset = pin.bondPeriodicOffset;
		p.style = ToPersisted(pin.style);
		for (std::size_t i = 0; i < pin.atomIndices.size(); ++i)
		{
			PersistedAtomRef ref;
			ref.index = pin.atomIndices[i];
			if (pin.linkBroken && i < pin.frozenAtomPositions.size())
			{
				ref.position = pin.frozenAtomPositions[i];
				if (i < pin.frozenAtomElements.size())
					ref.element = pin.frozenAtomElements[i];
			}
			else if (ref.index < window.structure.atoms.size())
			{
				ref.element = window.structure.atoms[ref.index].element;
				ref.position = window.structure.atoms[ref.index].cartesianPosition;
			}
			p.atomRefs.push_back(std::move(ref));
		}
		result.emplace_back(std::move(p));
	}
	for (const auto &label : window.freeLabels)
	{
		PersistedFreeLabel p;
		p.persistKey = label.persistKey;
		p.text = label.text;
		p.position = label.worldPosition;
		p.anchorAtoms = PersistOptionalAtomReference(window.structure, label.anchorAtom);
		if (label.anchorVacancy && *label.anchorVacancy <= static_cast<std::size_t>(std::numeric_limits<int>::max()))
			p.anchorVacancy = static_cast<int>(*label.anchorVacancy);
		p.anchorOffset = label.anchorOffset;
		p.rotationRadians = label.rotationRadians;
		p.style = ToPersisted(label.style);
		result.emplace_back(std::move(p));
	}
	for (const auto &orbital : window.sceneOrbitals)
	{
		PersistedSceneOrbital p;
		p.persistKey = orbital.persistKey;
		p.preset = OrbitalPresetName(orbital.preset);
		p.shell = orbital.shell;
		p.lobeIndex = orbital.lobeIndex;
		p.effectiveCharge = orbital.effectiveCharge;
		p.centerA = orbital.centerA;
		p.centerB = orbital.centerB;
		p.anchorAtoms = PersistAtomReferences(window.structure, orbital.anchorAtoms);
		p.rotationEuler = orbital.rotationEuler;
		p.phaseFlipped = orbital.phaseFlipped;
		p.scale = orbital.scale;
		p.stretch = orbital.stretch;
		p.isoFraction = orbital.isoFraction;
		p.resolution = orbital.resolution;
		p.positiveLobeColor = orbital.positiveLobeColor;
		p.negativeLobeColor = orbital.negativeLobeColor;
		p.alpha = orbital.alpha;
		p.visible = orbital.visible;
		p.displayName = orbital.displayName;
		for (const auto &component : orbital.lcaoComponents)
		{
			PersistedOrbitalLcaoComponent saved;
			const auto refs = PersistAtomReferences(window.structure, {component.anchorAtom});
			if (!refs.empty())
				saved.atom = refs.front();
			else
			{
				saved.atom.index = component.anchorAtom;
				saved.atom.position = component.center;
			}
			saved.preset = OrbitalPresetName(component.preset);
			saved.shell = component.shell;
			saved.lobeIndex = component.lobeIndex;
			saved.effectiveCharge = component.effectiveCharge;
			saved.rotationEuler = component.rotationEuler;
			saved.coefficient = component.coefficient;
			p.lcaoComponents.push_back(std::move(saved));
		}
		result.emplace_back(std::move(p));
	}
	for (const auto &plane : window.scenePlanes)
	{
		PersistedScenePlane p;
		p.persistKey = plane.persistKey;
		p.center = plane.center;
		p.normal = plane.normal;
		p.tangent = plane.tangent;
		p.halfExtents = plane.halfExtents;
		p.anchorAtoms = PersistAtomReferences(window.structure, plane.anchorAtoms);
		p.color = plane.color;
		p.alpha = plane.alpha;
		p.showBorder = plane.showBorder;
		p.visible = plane.visible;
		result.emplace_back(std::move(p));
	}
	if (window.paths != nullptr)
	{
		window.paths->Store().Visit(
			[&](const ScenePath &path) { result.emplace_back(ExtractPersistedScenePath(path, window)); });
	}
	return result;
}

void ApplyPersistedSceneObjects(RendererWindowState &window, const std::vector<PersistedSceneObject> &objects,
								std::vector<StructuredError> &warnings)
{
	window.pinnedMeasurements.clear();
	window.freeLabels.clear();
	window.sceneOrbitals.clear();
	window.scenePlanes.clear();
	if (window.paths != nullptr)
		window.paths->Clear();
	window.selectedScenePlanes.clear();
	window.selectedScenePaths.clear();
	window.selectedPinnedMeasurements.clear();
	window.selectedFreeLabels.clear();
	window.selectedSceneOrbitals.clear();
	std::vector<std::pair<SceneObjectId, std::reference_wrapper<const PersistedScenePath>>> loadedPaths;
	for (const auto &object : objects)
	{
		std::visit(
			[&](const auto &value)
			{
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, PersistedPinnedMeasurement>)
				{
					RendererWindowState::PinnedMeasurement p;
					p.id = window.sceneRegistry.AllocateObjectId();
					p.persistKey = value.persistKey.empty() ? GenerateScenePersistKey() : value.persistKey;
					p.worldOffset = value.labelOffset;
					p.alignToBondDirection = value.alignToBondDirection;
					p.flipped = value.flipped;
					p.rotationOffsetRadians = value.rotationOffsetRadians;
					p.bondPeriodicOffset = value.bondPeriodicOffset;
					p.style = FromPersisted(value.style);
					std::vector<std::optional<std::size_t>> resolvedIndices;
					resolvedIndices.reserve(value.atomRefs.size());
					for (const auto &ref : value.atomRefs)
						resolvedIndices.push_back(ResolveAtomReference(window.structure, ref));
					const bool binds = std::all_of(resolvedIndices.begin(), resolvedIndices.end(),
													 [](const auto &index) { return index.has_value(); });
					// A broken pin keeps the file's indices so a re-save writes the same references back.
					for (std::size_t i = 0; i < value.atomRefs.size(); ++i)
						p.atomIndices.push_back(resolvedIndices[i].value_or(value.atomRefs[i].index));
					// Re-evaluated on every load: a pin saved broken heals once its references resolve again,
					// otherwise it would stay frozen and never follow its atoms.
					p.linkBroken = !binds;
					if (p.linkBroken)
					{
						for (const auto &ref : value.atomRefs)
						{
							p.frozenAtomPositions.push_back(ref.position);
							p.frozenAtomElements.push_back(ref.element);
						}
					}
					if (!value.linkBroken && !binds)
						warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene object link is broken",
											  "A pinned measurement could not bind to the current structure",
											  "The measurement will remain at its saved position.",
											  "SceneObjectPersistence", "scene_objects.link_broken");
					window.pinnedMeasurements.push_back(std::move(p));
				}
				else if constexpr (std::is_same_v<T, PersistedFreeLabel>)
				{
					RendererWindowState::FreeLabel l;
					l.id = window.sceneRegistry.AllocateObjectId();
					l.persistKey = value.persistKey.empty() ? GenerateScenePersistKey() : value.persistKey;
					l.text = value.text;
					l.worldPosition = value.position;
					l.anchorAtom = ResolveOptionalAtomReference(window.structure, value.anchorAtoms);
					if (!l.anchorAtom && value.anchorVacancy && *value.anchorVacancy >= 0)
						l.anchorVacancy = static_cast<std::size_t>(*value.anchorVacancy);
					l.anchorOffset = value.anchorOffset;
					l.rotationRadians = value.rotationRadians;
					l.style = FromPersisted(value.style);
					window.freeLabels.push_back(std::move(l));
				}
				else if constexpr (std::is_same_v<T, PersistedSceneArrow>)
				{
					auto migrated = MigrateArrowToPath(value);
					if (!migrated)
					{
						warnings.push_back(migrated.Error());
						return;
					}
					for (const auto &warning : migrated.Value().warnings)
						warnings.push_back(warning);
					ScenePath path = std::move(migrated.Value().path);
					path.name = value.kind == PersistedArrowKind::Line ? "Line" : "Arrow";
					path.persistKey = value.persistKey.empty() ? GenerateScenePersistKey() : value.persistKey;
					const auto rebind = [&](PathNode &node, const std::vector<PersistedAtomRef> &references) {
						if (references.empty())
							return;
						const auto atom = ResolveOptionalAtomReference(window.structure, references);
						if (atom)
							node.binding = PathBinding{PathBinding::CopyPosition{*atom, {}, value.atomBuffer}};
						else
						{
							node.binding = PathBinding{};
							warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path binding is unresolved",
								"A migrated arrow endpoint could not bind to the current structure.",
								"The node keeps its saved position and is treated as free.",
								"SceneObjectPersistence", "scene_objects.path_binding_unresolved");
						}
					};
					rebind(path.nodes.front(), value.startAnchorAtoms);
					rebind(path.nodes.back(), value.endAnchorAtoms);
					MovePathOriginToCentre(path);
					(void)SceneSystem::AppendScenePath(window, std::move(path));
				}
				else if constexpr (std::is_same_v<T, PersistedSceneOrbital>)
				{
					RendererWindowState::SceneOrbital o;
					o.id = window.sceneRegistry.AllocateObjectId();
					o.persistKey = value.persistKey.empty() ? GenerateScenePersistKey() : value.persistKey;
					// A preset name this build does not know about degrades to p and says so,
					// rather than failing the whole load or being guessed at - a project written by
					// a newer version stays openable.
					if (!ParseOrbitalPreset(value.preset, o.preset))
						warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Unknown orbital preset",
											  "Orbital preset '" + value.preset + "' is not known to this build",
											  "The orbital was loaded as a p orbital.", "SceneObjectPersistence",
											  "scene_objects.unknown_orbital_preset");
					o.shell = value.shell;
					o.lobeIndex = value.lobeIndex;
					o.effectiveCharge = value.effectiveCharge;
					o.centerA = value.centerA;
					o.centerB = value.centerB;
					o.anchorAtoms = ResolveAtomReferences(window.structure, value.anchorAtoms);
					o.rotationEuler = value.rotationEuler;
					o.phaseFlipped = value.phaseFlipped;
					o.scale = value.scale;
					o.stretch = value.stretch;
					o.isoFraction = value.isoFraction;
					o.resolution = value.resolution;
					o.positiveLobeColor = value.positiveLobeColor;
					o.negativeLobeColor = value.negativeLobeColor;
					o.alpha = value.alpha;
					o.visible = value.visible;
					o.displayName = value.displayName;
					for (const auto &saved : value.lcaoComponents)
					{
						RendererWindowState::SceneOrbital::LcaoComponent component;
						if (!ParseOrbitalPreset(saved.preset, component.preset))
						{
							warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene orbital was skipped",
								"Unknown LCAO preset '" + saved.preset + "'.", "The orbital was not loaded.",
								"SceneObjectPersistence", "scene_objects.entry_skipped");
							return;
						}
						component.anchorAtom = ResolveAtomReference(window.structure, saved.atom).value_or(
							std::numeric_limits<std::size_t>::max());
						component.center = saved.atom.position;
						component.shell = saved.shell;
						component.lobeIndex = saved.lobeIndex;
						component.effectiveCharge = saved.effectiveCharge;
						component.rotationEuler = saved.rotationEuler;
						component.coefficient = saved.coefficient;
						o.lcaoComponents.push_back(component);
					}
					window.sceneOrbitals.push_back(std::move(o));
				}
				else if constexpr (std::is_same_v<T, PersistedScenePath>)
				{
					const Result<ScenePath> built = BuildScenePath(value, window.structure, warnings);
					if (built)
					{
						ScenePath path = built.Value();
						path.id = window.sceneRegistry.AllocateObjectId();
						const SceneObjectId id = path.id;
						if (!SceneSystem::EnsurePathSystem(window).Store().Insert(std::move(path)))
							warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path was skipped", "The path id collided while applying scene objects.", "The path was not loaded.", "SceneObjectPersistence", "scene_objects.entry_skipped");
						else
							loadedPaths.emplace_back(id, std::cref(value));
					}
				}
				else
				{
					RendererWindowState::ScenePlane pl;
					pl.id = window.sceneRegistry.AllocateObjectId();
					pl.persistKey = value.persistKey.empty() ? GenerateScenePersistKey() : value.persistKey;
					pl.center = value.center;
					pl.normal = glm::normalize(value.normal);
					// Re-orthogonalise on load: the file could have been hand-edited, and a tangent
					// that is not perpendicular to the normal makes ScenePlaneCorners produce a
					// skewed parallelogram rather than the rectangle everything else assumes.
					const glm::vec3 projected = value.tangent - glm::dot(value.tangent, pl.normal) * pl.normal;
					pl.tangent = glm::dot(projected, projected) > 1e-8f
						? glm::normalize(projected)
						: glm::normalize(glm::cross(pl.normal, std::abs(pl.normal.x) < 0.9f
															   ? glm::vec3(1.0f, 0.0f, 0.0f)
															   : glm::vec3(0.0f, 1.0f, 0.0f)));
					pl.halfExtents = value.halfExtents;
					pl.anchorAtoms = ResolveAtomReferences(window.structure, value.anchorAtoms);
					pl.color = value.color;
					pl.alpha = value.alpha;
					pl.showBorder = value.showBorder;
					pl.visible = value.visible;
					window.scenePlanes.push_back(std::move(pl));
				}
			},
			object);
	}
	for (const auto &[id, persisted] : loadedPaths)
		ResolveScenePathObjectOriginBindings(window, id, persisted.get(), warnings);
}

} // namespace DefectStudio
