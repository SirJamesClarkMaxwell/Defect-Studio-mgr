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
	p.outlineColor = s.outlineColor;
	p.outlineWidth = s.outlineWidth;
	p.headWidth = s.headWidth;
	p.headLength = s.headLength;
	return p;
}
static RendererWindowState::ArrowStyle FromPersisted(const PersistedArrowStyle &s)
{
	RendererWindowState::ArrowStyle p;
	p.color = s.color;
	p.alpha = s.alpha;
	p.shaftWidth = s.shaftWidth;
	p.outlineColor = s.outlineColor;
	p.outlineWidth = s.outlineWidth;
	p.headWidth = s.headWidth;
	p.headLength = s.headLength;
	return p;
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
	return result;
}

void ApplyPersistedSceneObjects(RendererWindowState &window, const std::vector<PersistedSceneObject> &objects,
								std::vector<StructuredError> &warnings)
{
	window.pinnedMeasurements.clear();
	window.freeLabels.clear();
	window.sceneArrows.clear();
	window.selectedPinnedMeasurements.clear();
	window.selectedFreeLabels.clear();
	window.selectedSceneArrows.clear();
	window.pinnedMeasurementUndoHistory.clear();
	window.pinnedMeasurementRedoHistory.clear();
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
					for (const auto &ref : value.atomRefs)
						p.atomIndices.push_back(ref.index);
					const bool binds = std::all_of(value.atomRefs.begin(), value.atomRefs.end(), [&](const auto &ref)
												   { return AtomReferenceBinds(window.structure, ref); });
					p.linkBroken = value.linkBroken || !binds;
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
				else
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
