#include "Core/dspch.hpp"

#include "IO/SceneObjectsIO.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>

#include <yaml-cpp/yaml.h>

#include "IO/SceneObjectsYaml.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
namespace
{
void Warn(std::vector<StructuredError> &warnings, const std::string &message)
{
	warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene object skipped", message,
						  "Repair or remove the invalid scene object before saving.", "SceneObjectsIO",
						  "scene_objects.entry_skipped");
}

using SceneObjectsYaml::EmitVec2;
using SceneObjectsYaml::EmitVec3;
using SceneObjectsYaml::Vec2;
using SceneObjectsYaml::Vec3;

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
		return (!node["textColor"] || Vec3(node["textColor"], style.textColor)) &&
			   (!node["backgroundColor"] || Vec3(node["backgroundColor"], style.backgroundColor)) &&
			   (!node["outlineColor"] || Vec3(node["outlineColor"], style.outlineColor)) &&
			   (!node["strokeColor"] || Vec3(node["strokeColor"], style.strokeColor)) &&
			   (!node["padding"] || Vec2(node["padding"], style.padding)) &&
			   (style.textAlpha = node["textAlpha"].as<float>(style.textAlpha), true) &&
			   (style.backgroundAlpha = node["backgroundAlpha"].as<float>(style.backgroundAlpha), true) &&
			   (style.outlineWidth = node["outlineWidth"].as<float>(style.outlineWidth), true) &&
			   (style.cornerRadius = node["cornerRadius"].as<float>(style.cornerRadius), true) &&
			   (style.strokeWidth = node["strokeWidth"].as<float>(style.strokeWidth), true) &&
			   (style.scale = node["scale"].as<float>(style.scale), true);
	}
	catch (const YAML::Exception &)
	{
		return false;
	}
}

[[nodiscard]] bool ParseAtomRefs(const YAML::Node &node, std::vector<PersistedAtomRef> &refs)
{
	if (!node || !node.IsSequence() || (node.size() != 2 && node.size() != 3))
		return false;
	try
	{
		refs.clear();
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

[[nodiscard]] bool ParseLabel(const YAML::Node &node, PersistedFreeLabel &label)
{
	try
	{
		if (!node["position"] || !Vec3(node["position"], label.position) ||
			!ParseLabelStyle(node["style"], label.style))
			return false;
		label.persistKey = node["persistKey"].as<std::string>("");
		label.text = node["text"].as<std::string>(label.text);
		label.rotationRadians = node["rotationRadians"].as<float>(label.rotationRadians);
		return true;
	}
	catch (const YAML::Exception &)
	{
		return false;
	}
}

[[nodiscard]] bool ParsePin(const YAML::Node &node, PersistedPinnedMeasurement &pin)
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

[[nodiscard]] bool ParseArrow(const YAML::Node &node, PersistedSceneArrow &arrow)
{
	try
	{
		const std::string kind = node["arrowKind"].as<std::string>("");
		if (kind == "Line")
			arrow.kind = PersistedArrowKind::Line;
		else if (kind == "Arrow2D")
			arrow.kind = PersistedArrowKind::Arrow2D;
		else if (kind == "Arrow3D")
			arrow.kind = PersistedArrowKind::Arrow3D;
		else
			return false;

		if (const YAML::Node points = node["points"])
		{
			if (!points.IsSequence() || points.size() < 2)
				return false;
			arrow.points.clear();
			arrow.points.reserve(points.size());
			for (const YAML::Node &pointNode : points)
			{
				glm::vec3 point(0.0f);
				if (!Vec3(pointNode, point))
					return false;
				arrow.points.push_back(point);
			}
		}
		else
		{
			// Pre-32b migration: old files stored the two endpoints as sibling `start`/`end`
			// keys. Preserve them verbatim as the first and last path points.
			glm::vec3 start(0.0f), end(0.0f);
			if (!node["start"] || !node["end"] || !Vec3(node["start"], start) || !Vec3(node["end"], end))
				return false;
			arrow.points = {start, end};
		}
		if (node["control_point"])
		{
			glm::vec3 controlPoint(0.0f);
			if (!Vec3(node["control_point"], controlPoint))
				return false;
			arrow.controlPoint = controlPoint;
		}
		arrow.curveSegments = node["curve_segments"].as<int>(arrow.curveSegments);
		arrow.startTip = node["start_tip"].as<std::string>("None");
		const char *legacyEndTip = arrow.kind == PersistedArrowKind::Line ? "None" : "Plain";
		arrow.endTip = node["end_tip"].as<std::string>(legacyEndTip);
		const std::string orientation = node["orientation2D"].as<std::string>("Billboard");
		if (orientation == "Billboard")
			arrow.orientation2D = PersistedArrow2DOrientation::Billboard;
		else if (orientation == "FixedPlane")
			arrow.orientation2D = PersistedArrow2DOrientation::FixedPlane;
		else
			return false;
		const std::string plane = node["fixedPlane"].as<std::string>("XY");
		if (plane == "XY")
			arrow.fixedPlane = PersistedWorldPlane::XY;
		else if (plane == "XZ")
			arrow.fixedPlane = PersistedWorldPlane::XZ;
		else if (plane == "YZ")
			arrow.fixedPlane = PersistedWorldPlane::YZ;
		else
			return false;
		arrow.persistKey = node["persistKey"].as<std::string>("");
		if (!SceneObjectsYaml::ParseAnchors(node["startAnchorAtoms"], arrow.startAnchorAtoms) ||
			arrow.startAnchorAtoms.size() > 1 ||
			!SceneObjectsYaml::ParseAnchors(node["endAnchorAtoms"], arrow.endAnchorAtoms) ||
			arrow.endAnchorAtoms.size() > 1)
			return false;
		arrow.atomBuffer = node["atom_buffer"].as<float>(arrow.atomBuffer);
		if (!node["style"] || !node["style"].IsMap())
			return true;
		if (node["style"]["color"] && !Vec3(node["style"]["color"], arrow.style.color))
			return false;
		arrow.style.alpha = node["style"]["alpha"].as<float>(arrow.style.alpha);
		arrow.style.shaftWidth = node["style"]["shaftWidth"].as<float>(arrow.style.shaftWidth);
		arrow.style.dashed = node["style"]["dashed"].as<bool>(arrow.style.dashed);
		arrow.style.dashLength = node["style"]["dash_length"].as<float>(arrow.style.dashLength);
		arrow.style.gapLength = node["style"]["gap_length"].as<float>(arrow.style.gapLength);
		if (node["style"]["outlineColor"] && !Vec3(node["style"]["outlineColor"], arrow.style.outlineColor))
			return false;
		arrow.style.outlineWidth = node["style"]["outlineWidth"].as<float>(arrow.style.outlineWidth);
		arrow.style.headWidth = node["style"]["headWidth"].as<float>(arrow.style.headWidth);
		arrow.style.headLength = node["style"]["headLength"].as<float>(arrow.style.headLength);
		arrow.style.useGradient = node["style"]["useGradient"].as<bool>(arrow.style.useGradient);
		if (node["style"]["gradientStart"] && !Vec3(node["style"]["gradientStart"], arrow.style.gradientStart))
			return false;
		if (node["style"]["gradientFinish"] && !Vec3(node["style"]["gradientFinish"], arrow.style.gradientFinish))
			return false;
		return true;
	}
	catch (const YAML::Exception &)
	{
		return false;
	}
}

void EmitStyle(YAML::Emitter &emit, const PersistedArrowStyle &style)
{
	emit << YAML::Key << "style" << YAML::Value << YAML::BeginMap;
	EmitVec3(emit, "color", style.color);
	emit << YAML::Key << "alpha" << YAML::Value << style.alpha << YAML::Key << "shaftWidth" << YAML::Value
		 << style.shaftWidth << YAML::Key << "dashed" << YAML::Value << style.dashed << YAML::Key
		 << "dash_length" << YAML::Value << style.dashLength << YAML::Key << "gap_length" << YAML::Value
		 << style.gapLength;
	EmitVec3(emit, "outlineColor", style.outlineColor);
	emit << YAML::Key << "outlineWidth" << YAML::Value << style.outlineWidth << YAML::Key << "headWidth" << YAML::Value
		 << style.headWidth << YAML::Key << "headLength" << YAML::Value << style.headLength << YAML::Key
		 << "useGradient" << YAML::Value << style.useGradient;
	EmitVec3(emit, "gradientStart", style.gradientStart);
	EmitVec3(emit, "gradientFinish", style.gradientFinish);
	emit << YAML::EndMap;
}
} // namespace

Path SceneObjectsIO::FilePath(const Path &projectDirectory) { return projectDirectory / "scene_objects.yaml"; }

std::string SceneObjectsIO::MakeStructureKey(const Path &projectDirectory, const Path &structureSourcePath)
{
	const auto project = projectDirectory.Native().lexically_normal();
	const auto source = structureSourcePath.Native().lexically_normal();
	const auto relative = source.lexically_relative(project);
	if (!relative.empty() && !relative.is_absolute() && relative.begin()->string() != "..")
		return relative.generic_string();
	return source.generic_string();
}

bool SceneObjectsIO::Parse(const std::string &text, SceneObjectsFile &outFile, std::vector<StructuredError> &warnings,
						   std::string &outError)
{
	outFile = {};
	outError.clear();
	try
	{
		if (text.find_first_not_of(" \t\r\n") == std::string::npos)
			return true;
		const YAML::Node root = YAML::Load(text);
		if (!root || !root.IsMap())
		{
			outError = "scene_objects.yaml root is not a map";
			return false;
		}
		outFile.formatVersion = root["formatVersion"].as<int>(kFormatVersion);
		if (outFile.formatVersion > kFormatVersion)
		{
			outError = "scene_objects.yaml uses a future format version: " + std::to_string(outFile.formatVersion);
			warnings.emplace_back(ErrorCategory::IO, Severity::Error, "Scene object file is from a newer version", outError,
				"Open this project in a newer DefectStudio build.", "SceneObjectsIO", "scene_objects.future_format_version");
			outFile = {};
			return false;
		}
		const YAML::Node structures = root["structures"];
		if (!structures)
			return true;
		if (!structures.IsSequence())
		{
			outError = "scene_objects.yaml structures is not a sequence";
			return false;
		}
		for (const YAML::Node &structureNode : structures)
		{
			std::string structureKey;
			try
			{
				structureKey =
					structureNode.IsMap() ? structureNode["structureKey"].as<std::string>("") : std::string();
			}
			catch (const YAML::Exception &)
			{
				structureKey.clear();
			}
			if (structureKey.empty())
			{
				Warn(warnings, "Structure entry has no structureKey");
				continue;
			}
			PersistedStructureSceneObjects structure;
			structure.structureKey = std::move(structureKey);
			const YAML::Node objects = structureNode["objects"];
			if (!objects)
			{
				outFile.structures.push_back(std::move(structure));
				continue;
			}
			if (!objects.IsSequence())
			{
				Warn(warnings, "Structure objects is not a sequence");
				continue;
			}
			for (const YAML::Node &node : objects)
			{
				if (!node.IsMap())
				{
					Warn(warnings, "Object is not a map");
					continue;
				}
				std::string kind;
				try
				{
					kind = node["kind"].as<std::string>("");
				}
				catch (const YAML::Exception &)
				{
					kind.clear();
				}
				bool valid = false;
				PersistedSceneObject object;
				if (kind == "PinnedMeasurement")
				{
					PersistedPinnedMeasurement value;
					valid = ParsePin(node, value);
					object = std::move(value);
				}
				else if (kind == "FreeLabel")
				{
					PersistedFreeLabel value;
					valid = ParseLabel(node, value);
					object = std::move(value);
				}
				else if (kind == "SceneArrow")
				{
					PersistedSceneArrow value;
					valid = ParseArrow(node, value);
					object = std::move(value);
				}
				else if (kind == "SceneOrbital")
				{
					PersistedSceneOrbital value;
					valid = SceneObjectsYaml::ParseOrbital(node, value);
					object = std::move(value);
				}
				else if (kind == "ScenePlane")
				{
					PersistedScenePlane value;
					valid = SceneObjectsYaml::ParsePlane(node, value);
					object = std::move(value);
				}
				else if (kind == "ScenePath")
				{
					PersistedScenePath value;
					valid = SceneObjectsYaml::ParsePath(node, value);
					object = std::move(value);
				}
				if (valid)
					structure.objects.push_back(std::move(object));
				else
					Warn(warnings, "Invalid or unknown scene object kind: " + kind);
			}
			outFile.structures.push_back(std::move(structure));
		}
		return true;
	}
	catch (const YAML::Exception &exception)
	{
		outError = exception.what();
		return false;
	}
}

std::string SceneObjectsIO::Serialize(const SceneObjectsFile &file)
{
	YAML::Emitter emit;
	emit << YAML::BeginMap << YAML::Key << "formatVersion" << YAML::Value << kFormatVersion << YAML::Key
		 << "structures" << YAML::Value << YAML::BeginSeq;
	for (const auto &structure : file.structures)
	{
		emit << YAML::BeginMap << YAML::Key << "structureKey" << YAML::Value << structure.structureKey << YAML::Key
			 << "objects" << YAML::Value << YAML::BeginSeq;
		for (const auto &object : structure.objects)
		{
			emit << YAML::BeginMap;
			std::visit(
				[&](const auto &value)
				{
					using T = std::decay_t<decltype(value)>;
					if constexpr (std::is_same_v<T, PersistedPinnedMeasurement>)
					{
						emit << YAML::Key << "kind" << YAML::Value << "PinnedMeasurement" << YAML::Key << "persistKey"
							 << YAML::Value << value.persistKey << YAML::Key << "atomRefs" << YAML::Value
							 << YAML::BeginSeq;
						for (const auto &ref : value.atomRefs)
						{
							emit << YAML::BeginMap << YAML::Key << "index" << YAML::Value << ref.index << YAML::Key
								 << "element" << YAML::Value << ref.element;
							EmitVec3(emit, "position", ref.position);
							emit << YAML::EndMap;
						}
						emit << YAML::EndSeq << YAML::Key << "linkBroken" << YAML::Value << value.linkBroken;
						EmitVec3(emit, "labelOffset", value.labelOffset);
						emit << YAML::Key << "alignToBondDirection" << YAML::Value << value.alignToBondDirection
							 << YAML::Key << "flipped" << YAML::Value << value.flipped << YAML::Key
							 << "rotationOffsetRadians" << YAML::Value << value.rotationOffsetRadians;
						EmitVec3(emit, "bondPeriodicOffset", value.bondPeriodicOffset);
						EmitLabelStyle(emit, value.style);
					}
					else if constexpr (std::is_same_v<T, PersistedFreeLabel>)
					{
						emit << YAML::Key << "kind" << YAML::Value << "FreeLabel" << YAML::Key << "persistKey"
							 << YAML::Value << value.persistKey << YAML::Key << "text" << YAML::Value << value.text;
						EmitVec3(emit, "position", value.position);
						emit << YAML::Key << "rotationRadians" << YAML::Value << value.rotationRadians;
						EmitLabelStyle(emit, value.style);
					}
					else if constexpr (std::is_same_v<T, PersistedSceneArrow>)
					{
						const char *arrowKind = value.kind == PersistedArrowKind::Line		? "Line"
												: value.kind == PersistedArrowKind::Arrow2D ? "Arrow2D"
																							: "Arrow3D";
						const char *orientation =
							value.orientation2D == PersistedArrow2DOrientation::Billboard ? "Billboard" : "FixedPlane";
						const char *plane = value.fixedPlane == PersistedWorldPlane::XY	  ? "XY"
											: value.fixedPlane == PersistedWorldPlane::XZ ? "XZ"
																						  : "YZ";
						emit << YAML::Key << "kind" << YAML::Value << "SceneArrow" << YAML::Key << "persistKey"
							 << YAML::Value << value.persistKey << YAML::Key << "arrowKind" << YAML::Value << arrowKind
							 << YAML::Key << "orientation2D" << YAML::Value << orientation << YAML::Key << "fixedPlane"
							 << YAML::Value << plane;
						emit << YAML::Key << "points" << YAML::Value << YAML::BeginSeq;
						for (const glm::vec3 &point : value.points)
							emit << YAML::Flow << YAML::BeginSeq << point.x << point.y << point.z << YAML::EndSeq;
						emit << YAML::EndSeq;
						if (value.controlPoint)
							EmitVec3(emit, "control_point", *value.controlPoint);
						emit << YAML::Key << "curve_segments" << YAML::Value << value.curveSegments
							 << YAML::Key << "start_tip" << YAML::Value << value.startTip
							 << YAML::Key << "end_tip" << YAML::Value << value.endTip;
						SceneObjectsYaml::EmitAnchors(emit, "startAnchorAtoms", value.startAnchorAtoms);
						SceneObjectsYaml::EmitAnchors(emit, "endAnchorAtoms", value.endAnchorAtoms);
						emit << YAML::Key << "atom_buffer" << YAML::Value << value.atomBuffer;
						EmitStyle(emit, value.style);
					}
					else if constexpr (std::is_same_v<T, PersistedSceneOrbital>)
					{
						SceneObjectsYaml::EmitOrbital(emit, value);
					}
					else if constexpr (std::is_same_v<T, PersistedScenePath>)
					{
						SceneObjectsYaml::EmitPath(emit, value);
					}
					else
					{
						SceneObjectsYaml::EmitPlane(emit, value);
					}
				},
				object);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::EndMap;
	}
	emit << YAML::EndSeq << YAML::EndMap;
	return emit.c_str();
}

bool SceneObjectsIO::Load(const Path &projectDirectory, SceneObjectsFile &outFile,
						  std::vector<StructuredError> &warnings, std::string &outError)
{
	outFile = {};
	std::string text;
	const Path path = FilePath(projectDirectory);
	if (!FileSystem::Exists(path.Native()))
		return true;
	if (!TextFileIO::Load(path, text, outError))
		return false;
	return Parse(text, outFile, warnings, outError);
}

bool SceneObjectsIO::Save(const Path &projectDirectory, const SceneObjectsFile &file, std::string &outError)
{
	outError.clear();
	if (!WriteBackupOnce(projectDirectory, outError))
		return false;
	const Path target = FilePath(projectDirectory);
	const Path temp = Path::FromResolved(target.Native().string() + ".tmp");
	// Serialize always stamps kFormatVersion, so a whole-scene copy just to overwrite one int would
	// buy nothing.
	if (!TextFileIO::Save(temp, Serialize(file), outError))
		return false;
	std::error_code error;
	if (!FileSystem::Rename(temp.Native(), target.Native(), error))
	{
		outError = error.message();
		FileSystem::Remove(temp.Native());
		return false;
	}
	return true;
}

bool SceneObjectsIO::WriteBackupOnce(const Path &projectDirectory, std::string &outError)
{
	outError.clear();
	const Path source = FilePath(projectDirectory);
	const Path backup = Path::FromResolved(source.Native().string() + ".v1.bak");
	if (!FileSystem::Exists(source.Native()) || FileSystem::Exists(backup.Native()))
		return true;
	std::string text;
	if (!TextFileIO::Load(source, text, outError))
		return false;
	try
	{
		const YAML::Node root = YAML::Load(text);
		if (!root || !root.IsMap() || root["formatVersion"].as<int>(kFormatVersion) != 1)
			return true;
	}
	catch (const YAML::Exception &exception)
	{
		outError = exception.what();
		return false;
	}
	std::error_code error;
	if (!FileSystem::Copy(source.Native(), backup.Native(), error))
	{
		outError = error ? error.message() : "Unable to create v1 scene object backup";
		return false;
	}
	return true;
}
} // namespace DefectStudio
