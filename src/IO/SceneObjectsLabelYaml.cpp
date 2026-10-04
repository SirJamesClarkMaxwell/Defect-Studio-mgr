#include "Core/dspch.hpp"

#include "IO/SceneObjectsYaml.hpp"

namespace DefectStudio::SceneObjectsYaml
{
	void EmitLabelStyle(YAML::Emitter &emit, const PersistedLabelStyle &style)
	{
		emit << YAML::Key << "style" << YAML::Value << YAML::BeginMap;
		EmitVec3(emit, "textColor", style.textColor);
		emit << YAML::Key << "textAlpha" << YAML::Value << style.textAlpha;
		EmitVec3(emit, "backgroundColor", style.backgroundColor);
		emit << YAML::Key << "backgroundAlpha" << YAML::Value << style.backgroundAlpha;
		EmitVec3(emit, "outlineColor", style.outlineColor);
		emit << YAML::Key << "outlineWidth" << YAML::Value << style.outlineWidth;
		emit << YAML::Key << "cornerRadius" << YAML::Value << style.cornerRadius;
		EmitVec2(emit, "padding", style.padding);
		EmitVec3(emit, "strokeColor", style.strokeColor);
		emit << YAML::Key << "strokeWidth" << YAML::Value << style.strokeWidth;
		emit << YAML::Key << "scale" << YAML::Value << style.scale << YAML::EndMap;
	}

	[[nodiscard]] bool ParseLabelStyle(const YAML::Node &node, PersistedLabelStyle &style)
	{
		if (!node || !node.IsMap())
			return true;
		try
		{
			if ((node["textColor"] && !Vec3(node["textColor"], style.textColor)) ||
				(node["backgroundColor"] && !Vec3(node["backgroundColor"], style.backgroundColor)) ||
				(node["outlineColor"] && !Vec3(node["outlineColor"], style.outlineColor)) ||
				(node["strokeColor"] && !Vec3(node["strokeColor"], style.strokeColor)) ||
				(node["padding"] && !Vec2(node["padding"], style.padding)))
				return false;
			style.textAlpha = node["textAlpha"].as<float>(style.textAlpha);
			style.backgroundAlpha = node["backgroundAlpha"].as<float>(style.backgroundAlpha);
			style.outlineWidth = node["outlineWidth"].as<float>(style.outlineWidth);
			style.cornerRadius = node["cornerRadius"].as<float>(style.cornerRadius);
			style.strokeWidth = node["strokeWidth"].as<float>(style.strokeWidth);
			style.scale = node["scale"].as<float>(style.scale);
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	bool ParseFreeLabel(const YAML::Node &node, PersistedFreeLabel &label)
	{
		try
		{
			if (!node["position"] || !Vec3(node["position"], label.position) ||
				!ParseLabelStyle(node["style"], label.style) ||
				!ParseAnchors(node["anchorAtoms"], label.anchorAtoms) || label.anchorAtoms.size() > 1 ||
				(node["anchorOffset"] && !Vec3(node["anchorOffset"], label.anchorOffset)))
				return false;
			label.persistKey = node["persistKey"].as<std::string>("");
			label.text = node["text"].as<std::string>(label.text);
			label.rotationRadians = node["rotationRadians"].as<float>(label.rotationRadians);
			if (node["anchorVacancy"])
			{
				label.anchorVacancy = node["anchorVacancy"].as<int>();
				if (*label.anchorVacancy < 0 || !label.anchorAtoms.empty())
					return false;
			}
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitFreeLabel(YAML::Emitter &emit, const PersistedFreeLabel &label)
	{
		emit << YAML::Key << "kind" << YAML::Value << "FreeLabel" << YAML::Key << "persistKey"
			<< YAML::Value << label.persistKey << YAML::Key << "text" << YAML::Value << label.text;
		EmitVec3(emit, "position", label.position);
		emit << YAML::Key << "rotationRadians" << YAML::Value << label.rotationRadians;
		if (!label.anchorAtoms.empty())
			EmitAnchors(emit, "anchorAtoms", label.anchorAtoms);
		if (label.anchorVacancy)
			emit << YAML::Key << "anchorVacancy" << YAML::Value << *label.anchorVacancy;
		if (!label.anchorAtoms.empty() || label.anchorVacancy || label.anchorOffset != glm::vec3(0.0f))
			EmitVec3(emit, "anchorOffset", label.anchorOffset);
		EmitLabelStyle(emit, label.style);
	}

	[[nodiscard]] static bool ParseAtomRefs(const YAML::Node &node, std::vector<PersistedAtomRef> &refs)
	{
		return node && node.IsSequence() && (node.size() == 2 || node.size() == 3) &&
			SceneObjectsYaml::ParseAnchors(node, refs);
	}

	bool ParsePinnedMeasurement(const YAML::Node &node, PersistedPinnedMeasurement &pin)
	{
		try
		{
			if (!ParseAtomRefs(node["atomRefs"], pin.atomRefs) || !ParseLabelStyle(node["style"], pin.style))
				return false;
			pin.persistKey = node["persistKey"].as<std::string>("");
			pin.linkBroken = node["linkBroken"].as<bool>(false);
			if (node["labelOffset"] && !Vec3(node["labelOffset"], pin.labelOffset))
				return false;
			if (node["bondPeriodicOffset"] && !Vec3(node["bondPeriodicOffset"], pin.bondPeriodicOffset))
				return false;
			pin.alignToBondDirection = node["alignToBondDirection"].as<bool>(pin.alignToBondDirection);
			pin.flipped = node["flipped"].as<bool>(pin.flipped);
			pin.rotationOffsetRadians = node["rotationOffsetRadians"].as<float>(pin.rotationOffsetRadians);
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitPinnedMeasurement(YAML::Emitter &emit, const PersistedPinnedMeasurement &pin)
	{
		emit << YAML::Key << "kind" << YAML::Value << "PinnedMeasurement" << YAML::Key << "persistKey"
			 << YAML::Value << pin.persistKey << YAML::Key << "atomRefs" << YAML::Value
			 << YAML::BeginSeq;
		for (const auto &ref : pin.atomRefs)
		{
			emit << YAML::BeginMap << YAML::Key << "index" << YAML::Value << ref.index << YAML::Key
				 << "element" << YAML::Value << ref.element;
			EmitVec3(emit, "position", ref.position);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::Key << "linkBroken" << YAML::Value << pin.linkBroken;
		EmitVec3(emit, "labelOffset", pin.labelOffset);
		emit << YAML::Key << "alignToBondDirection" << YAML::Value << pin.alignToBondDirection
			 << YAML::Key << "flipped" << YAML::Value << pin.flipped << YAML::Key
			 << "rotationOffsetRadians" << YAML::Value << pin.rotationOffsetRadians;
		EmitVec3(emit, "bondPeriodicOffset", pin.bondPeriodicOffset);
		EmitLabelStyle(emit, pin.style);
	}
} // namespace DefectStudio::SceneObjectsYaml
