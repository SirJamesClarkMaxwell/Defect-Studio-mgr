#pragma once

#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

#include "IO/SceneObjectsIO.hpp"

// The YAML shape of the scene-object kinds that did not fit in SceneObjectsIO.cpp. Orbitals carry
// fifteen fields where an arrow carries five, and adding them inline pushed that file past the
// ~500-line limit in AGENTS.md - so the vector helpers every kind shares moved here too, rather
// than being copied.
//
// Internal to IO. Nothing outside it should include this: SceneObjectsIO.hpp is the interface.
namespace DefectStudio::SceneObjectsYaml
{
	[[nodiscard]] bool Vec3(const YAML::Node &node, glm::vec3 &out);
	[[nodiscard]] bool Vec2(const YAML::Node &node, glm::vec2 &out);
	void EmitVec3(YAML::Emitter &emit, const char *key, const glm::vec3 &value);
	void EmitVec2(YAML::Emitter &emit, const char *key, const glm::vec2 &value);

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
} // namespace DefectStudio::SceneObjectsYaml
