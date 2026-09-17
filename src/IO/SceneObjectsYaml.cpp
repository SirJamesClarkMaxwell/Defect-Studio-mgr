#include "Core/dspch.hpp"

#include "IO/SceneObjectsYaml.hpp"

namespace DefectStudio::SceneObjectsYaml
{
	bool Vec3(const YAML::Node &node, glm::vec3 &out)
	{
		if (!node || !node.IsSequence() || node.size() != 3)
			return false;
		try
		{
			out = glm::vec3(node[0].as<float>(), node[1].as<float>(), node[2].as<float>());
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	bool Vec2(const YAML::Node &node, glm::vec2 &out)
	{
		if (!node || !node.IsSequence() || node.size() != 2)
			return false;
		try
		{
			out = glm::vec2(node[0].as<float>(), node[1].as<float>());
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitVec3(YAML::Emitter &emit, const char *key, const glm::vec3 &value)
	{
		emit << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z
			 << YAML::EndSeq;
	}

	void EmitVec2(YAML::Emitter &emit, const char *key, const glm::vec2 &value)
	{
		emit << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << value.x << value.y << YAML::EndSeq;
	}

	namespace
	{
		// Zero, one or two entries - unlike a pinned measurement's atomRefs, which must be exactly
		// two or three. An orbital with no anchors is the normal case (it floats where it was
		// dropped), so an absent or empty node is success, not a parse failure.
		[[nodiscard]] bool ParseAnchors(const YAML::Node &node, std::vector<PersistedAtomRef> &refs)
		{
			refs.clear();
			if (!node)
				return true;
			if (!node.IsSequence() || node.size() > 2)
				return false;
			try
			{
				refs.reserve(node.size());
				for (const YAML::Node &item : node)
				{
					if (!item.IsMap() || !item["index"] || !item["element"] || !item["position"])
						return false;
					PersistedAtomRef ref;
					ref.index = item["index"].as<std::size_t>();
					ref.element = item["element"].as<std::string>();
					if (!Vec3(item["position"], ref.position))
						return false;
					refs.push_back(std::move(ref));
				}
				return true;
			}
			catch (const YAML::Exception &)
			{
				return false;
			}
		}
	} // namespace

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
			if (node["positiveLobeColor"] && !Vec3(node["positiveLobeColor"], orbital.positiveLobeColor))
				return false;
			if (node["negativeLobeColor"] && !Vec3(node["negativeLobeColor"], orbital.negativeLobeColor))
				return false;
			if (!ParseAnchors(node["anchorAtoms"], orbital.anchorAtoms))
				return false;
			orbital.persistKey = node["persistKey"].as<std::string>("");
			orbital.shell = node["shell"].as<int>(orbital.shell);
			orbital.lobeIndex = node["lobeIndex"].as<int>(orbital.lobeIndex);
			orbital.effectiveCharge = node["effectiveCharge"].as<float>(orbital.effectiveCharge);
			orbital.scale = node["scale"].as<float>(orbital.scale);
			orbital.isoFraction = node["isoFraction"].as<float>(orbital.isoFraction);
			orbital.resolution = node["resolution"].as<int>(orbital.resolution);
			orbital.alpha = node["alpha"].as<float>(orbital.alpha);
			orbital.visible = node["visible"].as<bool>(orbital.visible);
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
		emit << YAML::Key << "anchorAtoms" << YAML::Value << YAML::BeginSeq;
		for (const PersistedAtomRef &ref : orbital.anchorAtoms)
		{
			emit << YAML::BeginMap << YAML::Key << "index" << YAML::Value << ref.index << YAML::Key << "element"
				 << YAML::Value << ref.element;
			EmitVec3(emit, "position", ref.position);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq;
		EmitVec3(emit, "rotationEuler", orbital.rotationEuler);
		emit << YAML::Key << "scale" << YAML::Value << orbital.scale << YAML::Key << "isoFraction" << YAML::Value
			 << orbital.isoFraction << YAML::Key << "resolution" << YAML::Value << orbital.resolution;
		EmitVec3(emit, "positiveLobeColor", orbital.positiveLobeColor);
		EmitVec3(emit, "negativeLobeColor", orbital.negativeLobeColor);
		emit << YAML::Key << "alpha" << YAML::Value << orbital.alpha << YAML::Key << "visible" << YAML::Value
			 << orbital.visible;
	}
} // namespace DefectStudio::SceneObjectsYaml
