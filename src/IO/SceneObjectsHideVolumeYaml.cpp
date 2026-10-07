#include "Core/dspch.hpp"

#include "IO/SceneObjectsYaml.hpp"

namespace DefectStudio::SceneObjectsYaml
{
	bool ParseHideVolume(const YAML::Node &node, PersistedSceneHideVolume &volume)
	{
		try
		{
			const std::string shape = node["shape"].as<std::string>(volume.kind);
			if (shape != "Sphere" && shape != "Box" && shape != "Cylinder")
				return false;
			const std::string frame = node["frame"].as<std::string>(volume.frame);
			if (frame != "Anchored" && frame != "Fractional")
				return false;
			if (!node["center"] || !Vec3(node["center"], volume.center))
				return false;
			if (node["rotationEuler"] && !Vec3(node["rotationEuler"], volume.rotationEuler))
				return false;
			if (node["halfExtents"] && !Vec3(node["halfExtents"], volume.halfExtents))
				return false;
			if (!ParseAnchors(node["anchorAtoms"], volume.anchorAtoms))
				return false;
			if (node["color"] && !Vec3(node["color"], volume.color))
				return false;
			volume.kind = shape;
			volume.frame = frame;
			volume.persistKey = node["persistKey"].as<std::string>("");
			volume.invert = node["invert"].as<bool>(volume.invert);
			volume.name = node["name"].as<std::string>("");
			volume.alpha = node["alpha"].as<float>(volume.alpha);
			volume.visible = node["visible"].as<bool>(volume.visible);
			volume.renderable = node["renderable"].as<bool>(volume.renderable);
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitHideVolume(YAML::Emitter &emit, const PersistedSceneHideVolume &volume)
	{
		emit << YAML::Key << "kind" << YAML::Value << "SceneHideVolume" << YAML::Key << "persistKey"
			 << YAML::Value << volume.persistKey << YAML::Key << "shape" << YAML::Value << volume.kind
			 << YAML::Key << "frame" << YAML::Value << volume.frame;
		EmitVec3(emit, "center", volume.center);
		EmitVec3(emit, "rotationEuler", volume.rotationEuler);
		EmitVec3(emit, "halfExtents", volume.halfExtents);
		EmitAnchors(emit, "anchorAtoms", volume.anchorAtoms);
		emit << YAML::Key << "invert" << YAML::Value << volume.invert << YAML::Key << "name" << YAML::Value
			 << volume.name;
		EmitVec3(emit, "color", volume.color);
		emit << YAML::Key << "alpha" << YAML::Value << volume.alpha << YAML::Key << "visible" << YAML::Value
			 << volume.visible << YAML::Key << "renderable" << YAML::Value << volume.renderable;
	}
} // namespace DefectStudio::SceneObjectsYaml
