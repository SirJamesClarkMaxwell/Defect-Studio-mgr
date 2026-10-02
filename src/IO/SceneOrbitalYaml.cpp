#include "Core/dspch.hpp"
#include "IO/SceneObjectsYaml.hpp"

#include <cmath>

namespace DefectStudio::SceneObjectsYaml
{
	bool ParseOrbital(const YAML::Node &node, PersistedSceneOrbital &orbital)
	{
		try
		{
			if (!node["preset"] || !node["centerA"] || !Vec3(node["centerA"], orbital.centerA))
				return false;
			orbital.preset = node["preset"].as<std::string>();
			if (orbital.preset.empty())
				return false;
			if (node["centerB"] && !Vec3(node["centerB"], orbital.centerB))
				return false;
			if (node["rotationEuler"] && !Vec3(node["rotationEuler"], orbital.rotationEuler))
				return false;
			if (node["stretch"] && !Vec3(node["stretch"], orbital.stretch))
				return false;
			if (node["positiveLobeColor"] && !Vec3(node["positiveLobeColor"], orbital.positiveLobeColor))
				return false;
			if (node["negativeLobeColor"] && !Vec3(node["negativeLobeColor"], orbital.negativeLobeColor))
				return false;
			if (!ParseAnchors(node["anchorAtoms"], orbital.anchorAtoms) || orbital.anchorAtoms.size() > 2)
				return false;
			orbital.persistKey = node["persistKey"].as<std::string>("");
			orbital.shell = node["shell"].as<int>(orbital.shell);
			orbital.lobeIndex = node["lobeIndex"].as<int>(orbital.lobeIndex);
			orbital.effectiveCharge = node["effectiveCharge"].as<float>(orbital.effectiveCharge);
			orbital.phaseFlipped = node["phaseFlipped"].as<bool>(orbital.phaseFlipped);
			orbital.scale = node["scale"].as<float>(orbital.scale);
			orbital.isoFraction = node["isoFraction"].as<float>(orbital.isoFraction);
			orbital.resolution = node["resolution"].as<int>(orbital.resolution);
			orbital.alpha = node["alpha"].as<float>(orbital.alpha);
			orbital.visible = node["visible"].as<bool>(orbital.visible);
			orbital.displayName = node["displayName"].as<std::string>("");
			const YAML::Node components = node["lcaoComponents"];
			if (components)
			{
				if (!components.IsSequence())
					return false;
				for (const YAML::Node &item : components)
				{
					if (!item.IsMap() || !item["atom"])
						return false;
					YAML::Node anchors(YAML::NodeType::Sequence);
					anchors.push_back(item["atom"]);
					std::vector<PersistedAtomRef> refs;
					if (!ParseAnchors(anchors, refs) || refs.size() != 1)
						return false;
					PersistedOrbitalLcaoComponent component;
					component.atom = refs.front();
					component.preset = item["preset"].as<std::string>(component.preset);
					component.shell = item["shell"].as<int>(component.shell);
					component.lobeIndex = item["lobeIndex"].as<int>(component.lobeIndex);
					component.effectiveCharge = item["effectiveCharge"].as<float>(component.effectiveCharge);
					component.coefficient = item["coefficient"].as<float>(component.coefficient);
					if (item["rotationEuler"] && !Vec3(item["rotationEuler"], component.rotationEuler))
						return false;
					if (!std::isfinite(component.coefficient) || !std::isfinite(component.effectiveCharge) ||
						component.effectiveCharge <= 0.0f)
						return false;
					for (int axis = 0; axis < 3; ++axis)
						if (!std::isfinite(component.atom.position[axis]) || !std::isfinite(component.rotationEuler[axis]))
							return false;
					orbital.lcaoComponents.push_back(std::move(component));
				}
			}
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitOrbital(YAML::Emitter &emit, const PersistedSceneOrbital &orbital)
	{
		emit << YAML::Key << "kind" << YAML::Value << "SceneOrbital" << YAML::Key << "persistKey" << YAML::Value
			 << orbital.persistKey << YAML::Key << "preset" << YAML::Value << orbital.preset << YAML::Key << "shell"
			 << YAML::Value << orbital.shell << YAML::Key << "lobeIndex" << YAML::Value << orbital.lobeIndex
			 << YAML::Key << "effectiveCharge" << YAML::Value << orbital.effectiveCharge;
		EmitVec3(emit, "centerA", orbital.centerA);
		EmitVec3(emit, "centerB", orbital.centerB);
		EmitAnchors(emit, "anchorAtoms", orbital.anchorAtoms);
		EmitVec3(emit, "rotationEuler", orbital.rotationEuler);
		emit << YAML::Key << "phaseFlipped" << YAML::Value << orbital.phaseFlipped << YAML::Key << "scale"
			 << YAML::Value << orbital.scale;
		EmitVec3(emit, "stretch", orbital.stretch);
		emit << YAML::Key << "isoFraction" << YAML::Value << orbital.isoFraction << YAML::Key << "resolution"
			 << YAML::Value << orbital.resolution;
		EmitVec3(emit, "positiveLobeColor", orbital.positiveLobeColor);
		EmitVec3(emit, "negativeLobeColor", orbital.negativeLobeColor);
		emit << YAML::Key << "alpha" << YAML::Value << orbital.alpha << YAML::Key << "visible" << YAML::Value
			 << orbital.visible;
		if (!orbital.displayName.empty())
			emit << YAML::Key << "displayName" << YAML::Value << orbital.displayName;
		if (!orbital.lcaoComponents.empty())
		{
			emit << YAML::Key << "lcaoComponents" << YAML::Value << YAML::BeginSeq;
			for (const auto &component : orbital.lcaoComponents)
			{
				emit << YAML::BeginMap << YAML::Key << "atom" << YAML::Value << YAML::BeginMap
					<< YAML::Key << "index" << YAML::Value << component.atom.index
					<< YAML::Key << "element" << YAML::Value << component.atom.element;
				EmitVec3(emit, "position", component.atom.position);
				emit << YAML::EndMap << YAML::Key << "preset" << YAML::Value << component.preset
					<< YAML::Key << "shell" << YAML::Value << component.shell
					<< YAML::Key << "lobeIndex" << YAML::Value << component.lobeIndex
					<< YAML::Key << "effectiveCharge" << YAML::Value << component.effectiveCharge
					<< YAML::Key << "coefficient" << YAML::Value << component.coefficient;
				EmitVec3(emit, "rotationEuler", component.rotationEuler);
				emit << YAML::EndMap;
			}
			emit << YAML::EndSeq;
		}
	}

} // namespace DefectStudio::SceneObjectsYaml
