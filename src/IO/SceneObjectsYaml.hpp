#pragma once

#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

#include "IO/SceneObjectsIO.hpp"

// The YAML shape of the scene-object kinds that did not fit in SceneObjectsIO.cpp. Orbitals and
// planes pushed that file toward the ~500-line limit in AGENTS.md, so the anchor helpers every
// anchored kind shares live here too rather than being copied.
//
// Internal to IO. Nothing outside it should include this: SceneObjectsIO.hpp is the interface.
namespace DefectStudio::SceneObjectsYaml
{
	[[nodiscard]] bool Vec3(const YAML::Node &node, glm::vec3 &out);
	[[nodiscard]] bool Vec2(const YAML::Node &node, glm::vec2 &out);
	void EmitVec3(YAML::Emitter &emit, const char *key, const glm::vec3 &value);
	void EmitVec2(YAML::Emitter &emit, const char *key, const glm::vec2 &value);
	[[nodiscard]] bool ParseAnchors(const YAML::Node &node, std::vector<PersistedAtomRef> &refs);
	void EmitAnchors(YAML::Emitter &emit, const char *key, const std::vector<PersistedAtomRef> &refs);

	bool ParseLabelStyle(const YAML::Node &node, PersistedLabelStyle &style);
	void EmitLabelStyle(YAML::Emitter &emit, const PersistedLabelStyle &style);
	bool ParsePinnedMeasurement(const YAML::Node &node, PersistedPinnedMeasurement &pin);
	void EmitPinnedMeasurement(YAML::Emitter &emit, const PersistedPinnedMeasurement &pin);
	bool ParseFreeLabel(const YAML::Node &node, PersistedFreeLabel &label);
	void EmitFreeLabel(YAML::Emitter &emit, const PersistedFreeLabel &label);

	// Returns false for a node missing a required key (`preset`, `centerA`) or holding a value of
	// the wrong shape; `orbital` is then left in whatever state it was in and the caller drops it
	// with a warning. Every other key is optional and falls back to the struct's own default, so a
	// file written by an older version loads.
	[[nodiscard]] bool ParseOrbital(const YAML::Node &node, PersistedSceneOrbital &orbital);
	void EmitOrbital(YAML::Emitter &emit, const PersistedSceneOrbital &orbital);

	// Returns false without `center` or `normal`, or for a normal of zero length - a plane with no
	// orientation has no quad to draw and is dropped with a warning rather than rendered edge-on.
	[[nodiscard]] bool ParsePlane(const YAML::Node &node, PersistedScenePlane &plane);
	void EmitPlane(YAML::Emitter &emit, const PersistedScenePlane &plane);

	[[nodiscard]] bool ParseHideVolume(const YAML::Node &node, PersistedSceneHideVolume &volume);
	void EmitHideVolume(YAML::Emitter &emit, const PersistedSceneHideVolume &volume);

	[[nodiscard]] bool ParsePath(const YAML::Node &node, PersistedScenePath &path);
	void EmitPath(YAML::Emitter &emit, const PersistedScenePath &path);
} // namespace DefectStudio::SceneObjectsYaml
