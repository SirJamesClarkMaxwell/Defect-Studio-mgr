#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneObjectPersistence.hpp"

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>
#include <type_traits>
#include <unordered_map>

#include "Core/Utils/Uuid.hpp"

namespace DefectStudio
{
bool AtomReferenceBinds(const RendererStructureData &structure, const PersistedAtomRef &reference, float tolerance)
{
	if (reference.index >= structure.atoms.size() || structure.atoms[reference.index].element != reference.element)
		return false;
	const glm::vec3 delta = structure.atoms[reference.index].cartesianPosition - reference.position;
	return glm::dot(delta, delta) <= tolerance * tolerance;
}

std::optional<std::size_t> ResolveAtomReference(
	const RendererStructureData &structure, const PersistedAtomRef &reference, float tolerance)
{
	if (AtomReferenceBinds(structure, reference, tolerance))
		return reference.index;

	const float toleranceSquared = tolerance * tolerance;
	std::optional<std::size_t> nearest;
	float nearestDistanceSquared = toleranceSquared;
	for (std::size_t index = 0; index < structure.atoms.size(); ++index)
	{
		if (structure.atoms[index].element != reference.element)
			continue;
		const glm::vec3 delta = structure.atoms[index].cartesianPosition - reference.position;
		const float distanceSquared = glm::dot(delta, delta);
		if (distanceSquared <= nearestDistanceSquared)
		{
			nearest = index;
			nearestDistanceSquared = distanceSquared;
		}
	}
	return nearest;
}

std::string GenerateScenePersistKey()
{
	std::string key = ToString(GenerateUuid());
	key.erase(std::remove(key.begin(), key.end(), '-'), key.end());
	return key;
}

void EnsureScenePersistKeys(RendererWindowState &window)
{
	for (auto &pin : window.pinnedMeasurements)
		if (pin.persistKey.empty())
			pin.persistKey = GenerateScenePersistKey();
	for (auto &label : window.freeLabels)
		if (label.persistKey.empty())
			label.persistKey = GenerateScenePersistKey();
	for (auto &arrow : window.sceneArrows)
		if (arrow.persistKey.empty())
			arrow.persistKey = GenerateScenePersistKey();
	for (auto &orbital : window.sceneOrbitals)
		if (orbital.persistKey.empty())
			orbital.persistKey = GenerateScenePersistKey();
	for (auto &plane : window.scenePlanes)
		if (plane.persistKey.empty())
			plane.persistKey = GenerateScenePersistKey();
}

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
static PersistedArrowStyle ToPersisted(const RendererWindowState::ArrowStyle &s)
{
	PersistedArrowStyle p;
	p.color = s.color;
	p.alpha = s.alpha;
	p.shaftWidth = s.shaftWidth;
	p.dashed = s.dashed;
	p.dashLength = s.dashLength;
	p.gapLength = s.gapLength;
	p.outlineColor = s.outlineColor;
	p.outlineWidth = s.outlineWidth;
	p.headWidth = s.headWidth;
	p.headLength = s.headLength;
	p.useGradient = s.useGradient;
	p.gradientStart = s.gradient.start;
	p.gradientFinish = s.gradient.finish;
	return p;
}
static RendererWindowState::ArrowStyle FromPersisted(const PersistedArrowStyle &s)
{
	RendererWindowState::ArrowStyle p;
	p.color = s.color;
	p.alpha = s.alpha;
	p.shaftWidth = s.shaftWidth;
	p.dashed = s.dashed;
	p.dashLength = s.dashLength;
	p.gapLength = s.gapLength;
	p.outlineColor = s.outlineColor;
	p.outlineWidth = s.outlineWidth;
	p.headWidth = s.headWidth;
	p.headLength = s.headLength;
	p.useGradient = s.useGradient;
	p.gradient.start = s.gradientStart;
	p.gradient.finish = s.gradientFinish;
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
		p.rotationRadians = label.rotationRadians;
		p.style = ToPersisted(label.style);
		result.emplace_back(std::move(p));
	}
	for (const auto &arrow : window.sceneArrows)
	{
		PersistedSceneArrow p;
		p.persistKey = arrow.persistKey;
		p.kind = static_cast<PersistedArrowKind>(arrow.kind);
		p.orientation2D = static_cast<PersistedArrow2DOrientation>(arrow.orientation2D);
		p.fixedPlane = static_cast<PersistedWorldPlane>(arrow.fixedPlane);
		p.start = arrow.start;
		p.end = arrow.end;
		p.style = ToPersisted(arrow.style);
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
		p.scale = orbital.scale;
		p.isoFraction = orbital.isoFraction;
		p.resolution = orbital.resolution;
		p.positiveLobeColor = orbital.positiveLobeColor;
		p.negativeLobeColor = orbital.negativeLobeColor;
		p.alpha = orbital.alpha;
		p.visible = orbital.visible;
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
	return result;
}

void ApplyPersistedSceneObjects(RendererWindowState &window, const std::vector<PersistedSceneObject> &objects,
								std::vector<StructuredError> &warnings)
{
	window.pinnedMeasurements.clear();
	window.freeLabels.clear();
	window.sceneArrows.clear();
	window.sceneOrbitals.clear();
	window.scenePlanes.clear();
	window.selectedScenePlanes.clear();
	window.selectedPinnedMeasurements.clear();
	window.selectedFreeLabels.clear();
	window.selectedSceneArrows.clear();
	window.selectedSceneOrbitals.clear();
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
					l.rotationRadians = value.rotationRadians;
					l.style = FromPersisted(value.style);
					window.freeLabels.push_back(std::move(l));
				}
				else if constexpr (std::is_same_v<T, PersistedSceneArrow>)
				{
					RendererWindowState::SceneArrow a;
					a.id = window.sceneRegistry.AllocateObjectId();
					a.persistKey = value.persistKey.empty() ? GenerateScenePersistKey() : value.persistKey;
					a.kind = static_cast<RendererWindowState::ArrowKind>(value.kind);
					a.orientation2D = static_cast<RendererWindowState::Arrow2DOrientation>(value.orientation2D);
					a.fixedPlane = static_cast<RendererWindowState::WorldPlane>(value.fixedPlane);
					a.start = value.start;
					a.end = value.end;
					a.style = FromPersisted(value.style);
					window.sceneArrows.push_back(std::move(a));
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
					o.scale = value.scale;
					o.isoFraction = value.isoFraction;
					o.resolution = value.resolution;
					o.positiveLobeColor = value.positiveLobeColor;
					o.negativeLobeColor = value.negativeLobeColor;
					o.alpha = value.alpha;
					o.visible = value.visible;
					window.sceneOrbitals.push_back(std::move(o));
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
}

std::vector<PersistedSceneObject> MergeWindowSceneObjects(const std::vector<std::vector<PersistedSceneObject>> &windows)
{
	std::vector<PersistedSceneObject> merged;
	std::unordered_map<std::string, std::size_t> positions;
	for (const auto &window : windows)
		for (const auto &object : window)
		{
			const std::string key = std::visit([](const auto &v) { return v.persistKey; }, object);
			if (key.empty())
			{
				merged.push_back(object);
				continue;
			}
			auto it = positions.find(key);
			if (it == positions.end())
			{
				positions.emplace(key, merged.size());
				merged.push_back(object);
			}
			else
				merged[it->second] = object;
		}
	return merged;
}
} // namespace DefectStudio
