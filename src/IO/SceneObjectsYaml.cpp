#include "Core/dspch.hpp"

#include "IO/SceneObjectsYaml.hpp"

#include <cmath>

namespace DefectStudio::SceneObjectsYaml
{
	bool ParseVacancy(const YAML::Node &node, PersistedVacancy &vacancy)
	{
		try
		{
			if (!node.IsMap() || !Vec3(node["position"], vacancy.position) ||
				(node["fractional"] && !Vec3(node["fractional"], vacancy.fractional)))
				return false;
			vacancy.sourceSpecies = node["sourceSpecies"].as<std::string>("");
			vacancy.label = node["label"].as<std::string>("");
			vacancy.index = node["index"].as<int>(0);
			if (glm::vec3 color; node["color"] && Vec3(node["color"], color))
				vacancy.color = color;
			vacancy.hidden = node["hidden"].as<bool>(false);
			return std::isfinite(vacancy.position.x) && std::isfinite(vacancy.position.y) &&
				std::isfinite(vacancy.position.z) && std::isfinite(vacancy.fractional.x) &&
				std::isfinite(vacancy.fractional.y) && std::isfinite(vacancy.fractional.z);
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitVacancies(YAML::Emitter &emit, const std::vector<PersistedVacancy> &vacancies)
	{
		if (vacancies.empty())
			return;
		emit << YAML::Key << "vacancies" << YAML::Value << YAML::BeginSeq;
		for (const auto &vacancy : vacancies)
		{
			emit << YAML::BeginMap;
			EmitVec3(emit, "position", vacancy.position);
			EmitVec3(emit, "fractional", vacancy.fractional);
			emit << YAML::Key << "sourceSpecies" << YAML::Value << vacancy.sourceSpecies
				<< YAML::Key << "label" << YAML::Value << vacancy.label
				<< YAML::Key << "index" << YAML::Value << vacancy.index;
			if (vacancy.color)
				EmitVec3(emit, "color", *vacancy.color);
			if (vacancy.hidden)
				emit << YAML::Key << "hidden" << YAML::Value << true;
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq;
	}

	bool ParseDefectFrame(const YAML::Node &node, PersistedDefectFrame &frame)
	{
		auto finite = [](const glm::vec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
		try
		{
			if (!(node.IsMap() && Vec3(node["origin"], frame.origin) && Vec3(node["x"], frame.x) &&
					Vec3(node["y"], frame.y) && Vec3(node["z"], frame.z) && finite(frame.origin) && finite(frame.x) &&
					finite(frame.y) && finite(frame.z)))
				return false;
			// Display keys are best-effort: a bad one is dropped, the frame itself still loads.
			const auto positive = [&](const char *key, std::optional<float> &out) {
				if (const YAML::Node value = node[key]; value && value.IsScalar())
					if (const float number = value.as<float>(0.0f); std::isfinite(number) && number > 0.0f)
						out = number;
			};
			positive("axisLength", frame.axisLength);
			positive("axisWidth", frame.axisWidth);
			if (const YAML::Node value = node["negativeAxes"]; value && value.IsScalar())
				frame.negativeAxes = value.as<bool>(true);
			if (const YAML::Node value = node["shown"]; value && value.IsScalar())
				frame.shown = value.as<bool>(true);
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitDefectFrame(YAML::Emitter &emit, const std::optional<PersistedDefectFrame> &frame)
	{
		if (!frame)
			return;
		emit << YAML::Key << "defectFrame" << YAML::Value << YAML::BeginMap;
		EmitVec3(emit, "origin", frame->origin);
		EmitVec3(emit, "x", frame->x);
		EmitVec3(emit, "y", frame->y);
		EmitVec3(emit, "z", frame->z);
		if (frame->axisLength)
			emit << YAML::Key << "axisLength" << YAML::Value << *frame->axisLength;
		if (frame->axisWidth)
			emit << YAML::Key << "axisWidth" << YAML::Value << *frame->axisWidth;
		if (frame->negativeAxes)
			emit << YAML::Key << "negativeAxes" << YAML::Value << *frame->negativeAxes;
		if (frame->shown)
			emit << YAML::Key << "shown" << YAML::Value << *frame->shown;
		emit << YAML::EndMap;
	}

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

	bool Vec4(const YAML::Node &node, glm::vec4 &out)
	{
		if (!node || !node.IsSequence() || node.size() != 4)
			return false;
		try
		{
			out = glm::vec4(node[0].as<float>(), node[1].as<float>(), node[2].as<float>(), node[3].as<float>());
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitVec4(YAML::Emitter &emit, const char *key, const glm::vec4 &value)
	{
		emit << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z << value.w
			 << YAML::EndSeq;
	}

	// An absent or empty anchor list is normal for free-standing objects. Cardinality belongs to
	// the object parser: arrows cap each endpoint at one, orbitals at two, and a fitted plane may
	// keep every selected atom.
	bool ParseAnchors(const YAML::Node &node, std::vector<PersistedAtomRef> &refs)
	{
		refs.clear();
		if (!node)
			return true;
		if (!node.IsSequence())
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

	void EmitAnchors(YAML::Emitter &emit, const char *key, const std::vector<PersistedAtomRef> &refs)
	{
		emit << YAML::Key << key << YAML::Value << YAML::BeginSeq;
		for (const PersistedAtomRef &ref : refs)
		{
			emit << YAML::BeginMap << YAML::Key << "index" << YAML::Value << ref.index << YAML::Key
				 << "element" << YAML::Value << ref.element;
			EmitVec3(emit, "position", ref.position);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq;
	}

	bool ParsePlane(const YAML::Node &node, PersistedScenePlane &plane)
	{
		try
		{
			if (!node["center"] || !Vec3(node["center"], plane.center))
				return false;
			if (!node["normal"] || !Vec3(node["normal"], plane.normal))
				return false;
			if (glm::dot(plane.normal, plane.normal) <= 1e-8f)
				return false;
			if (node["tangent"] && !Vec3(node["tangent"], plane.tangent))
				return false;
			if (node["halfExtents"] && !Vec2(node["halfExtents"], plane.halfExtents))
				return false;
			if (node["color"] && !Vec3(node["color"], plane.color))
				return false;
			if (!ParseAnchors(node["anchorAtoms"], plane.anchorAtoms))
				return false;
			plane.persistKey = node["persistKey"].as<std::string>("");
			plane.alpha = node["alpha"].as<float>(plane.alpha);
			plane.showBorder = node["showBorder"].as<bool>(plane.showBorder);
			plane.visible = node["visible"].as<bool>(plane.visible);
			return true;
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitPlane(YAML::Emitter &emit, const PersistedScenePlane &plane)
	{
		emit << YAML::Key << "kind" << YAML::Value << "ScenePlane" << YAML::Key << "persistKey" << YAML::Value
			 << plane.persistKey;
		EmitVec3(emit, "center", plane.center);
		EmitVec3(emit, "normal", plane.normal);
		EmitVec3(emit, "tangent", plane.tangent);
		EmitVec2(emit, "halfExtents", plane.halfExtents);
		EmitAnchors(emit, "anchorAtoms", plane.anchorAtoms);
		EmitVec3(emit, "color", plane.color);
		emit << YAML::Key << "alpha" << YAML::Value << plane.alpha << YAML::Key << "showBorder" << YAML::Value
			 << plane.showBorder << YAML::Key << "visible" << YAML::Value << plane.visible;
	}

	namespace
	{
		bool ParseBinding(const YAML::Node &node, PersistedPathBinding &binding)
		{
			try
			{
				if (!node)
					return true;
				if (!node.IsMap())
					return false;
				binding.kind = node["kind"].as<std::string>(binding.kind);
				if (binding.kind != "Free" && binding.kind != "CopyPosition" && binding.kind != "CopyVacancy" && binding.kind != "BondMidpoint" && binding.kind != "ObjectOrigin")
					return false;
				if (binding.kind == "CopyVacancy")
				{
					if (!node["vacancyIndex"])
						return false;
					binding.vacancyIndex = node["vacancyIndex"].as<std::size_t>();
				}
				if (!ParseAnchors(node["atoms"], binding.atoms))
					return false;
				if (node["offset"] && !Vec3(node["offset"], binding.offset))
					return false;
				binding.buffer = node["buffer"].as<float>(binding.buffer);
				binding.objectPersistKey = node["objectPersistKey"].as<std::string>(binding.objectPersistKey);
				if ((binding.kind == "CopyPosition" && binding.atoms.size() != 1) ||
					(binding.kind == "BondMidpoint" && binding.atoms.size() != 2) ||
					((binding.kind == "Free" || binding.kind == "ObjectOrigin" || binding.kind == "CopyVacancy") && !binding.atoms.empty()))
					return false;
				return true;
			}
			catch (const YAML::Exception &)
			{
				return false;
			}
		}

		bool ParsePathStyle(const YAML::Node &node, PersistedPathStyle &style)
		{
			try
			{
				if (!node)
					return true;
				if (!node.IsMap())
					return false;
				style.profile = node["profile"].as<std::string>(style.profile);
				style.join = node["join"].as<std::string>(style.join);
				style.cap = node["cap"].as<std::string>(style.cap);
				style.depthMode = node["depthMode"].as<std::string>(style.depthMode);
				if (style.profile != "Round" && style.profile != "Flat" && style.profile != "CameraFacing") return false;
				if (style.join != "Bevel" && style.join != "Round") return false;
				if (style.cap != "Butt" && style.cap != "Square" && style.cap != "Round") return false;
				if (style.depthMode != "DepthTest" && style.depthMode != "AlwaysOnTop") return false;
				style.width = node["width"].as<float>(style.width);
				if (node["ribbon_normal"] && !Vec3(node["ribbon_normal"], style.ribbonNormal)) return false;
				style.ribbonThickness = node["ribbon_thickness"].as<float>(style.ribbonThickness);
				style.ribbonBevel = node["ribbon_bevel"].as<float>(style.ribbonBevel);
				style.ribbonBevelParts = node["ribbon_bevel_parts"].as<std::string>(style.ribbonBevelParts);
				if (style.ribbonBevelParts != "Both" && style.ribbonBevelParts != "Shaft" && style.ribbonBevelParts != "Decorations") return false;
				style.ribbonBevelSegments = node["ribbon_bevel_segments"].as<int>(style.ribbonBevelSegments);
				style.ribbonBevelShape = node["ribbon_bevel_shape"].as<float>(style.ribbonBevelShape);
				style.shadeSmooth = node["shade_smooth"].as<bool>(style.shadeSmooth);
				style.radialSegments = node["radialSegments"].as<int>(style.radialSegments);
				if (node["color"] && !Vec3(node["color"], style.color)) return false;
				style.alpha = node["alpha"].as<float>(style.alpha);
				style.dashEnabled = node["dashEnabled"].as<bool>(style.dashEnabled);
				style.dashLength = node["dashLength"].as<float>(style.dashLength);
				style.gapLength = node["gapLength"].as<float>(style.gapLength);
				style.dashPhase = node["dashPhase"].as<float>(style.dashPhase);
				style.gradientEnabled = node["gradientEnabled"].as<bool>(style.gradientEnabled);
				const YAML::Node stops = node["gradientStops"];
				if (stops)
				{
					if (!stops.IsSequence()) return false;
					style.gradientStops.clear();
					for (const YAML::Node &stopNode : stops)
					{
						PersistedPathGradientStop stop;
						if (!stopNode.IsMap() || !stopNode["color"] || !Vec3(stopNode["color"], stop.color)) return false;
						stop.position = stopNode["position"].as<float>(stop.position);
						stop.alpha = stopNode["alpha"].as<float>(stop.alpha);
						style.gradientStops.push_back(stop);
					}
				}
				style.startDecoration = node["startDecoration"].as<std::string>(style.startDecoration);
				style.startDecorationLengthScale = node["startDecorationLengthScale"].as<float>(style.startDecorationLengthScale);
				style.startDecorationWidthScale = node["startDecorationWidthScale"].as<float>(style.startDecorationWidthScale);
				style.startDecorationFilled = node["start_decoration_filled"].as<bool>(style.startDecorationFilled);
				style.endDecoration = node["endDecoration"].as<std::string>(style.endDecoration);
				style.endDecorationLengthScale = node["endDecorationLengthScale"].as<float>(style.endDecorationLengthScale);
				style.endDecorationWidthScale = node["endDecorationWidthScale"].as<float>(style.endDecorationWidthScale);
				style.endDecorationFilled = node["end_decoration_filled"].as<bool>(style.endDecorationFilled);
				const auto validDecoration = [](const std::string &name) {
					return name == "None" || name == "Arrow" || name == "Stealth" || name == "Latex" || name == "Bar" || name == "Circle" || name == "Square" || name == "Diamond" || name == "Kite" || name == "OpenArrow";
				};
				return validDecoration(style.startDecoration) && validDecoration(style.endDecoration);
			}
			catch (const YAML::Exception &)
			{
				return false;
			}
		}

		void EmitBinding(YAML::Emitter &emit, const PersistedPathBinding &binding)
		{
			emit << YAML::Key << "binding" << YAML::Value << YAML::BeginMap << YAML::Key << "kind" << YAML::Value << binding.kind;
			if (binding.kind == "CopyVacancy")
				emit << YAML::Key << "vacancyIndex" << YAML::Value << binding.vacancyIndex;
			EmitAnchors(emit, "atoms", binding.atoms);
			EmitVec3(emit, "offset", binding.offset);
			emit << YAML::Key << "buffer" << YAML::Value << binding.buffer << YAML::Key << "objectPersistKey" << YAML::Value << binding.objectPersistKey << YAML::EndMap;
		}

		void EmitPathStyle(YAML::Emitter &emit, const PersistedPathStyle &style)
		{
			emit << YAML::Key << "style" << YAML::Value << YAML::BeginMap << YAML::Key << "profile" << YAML::Value << style.profile << YAML::Key << "ribbon_normal" << YAML::Value << YAML::Flow << YAML::BeginSeq << style.ribbonNormal.x << style.ribbonNormal.y << style.ribbonNormal.z << YAML::EndSeq << YAML::Key << "ribbon_thickness" << YAML::Value << style.ribbonThickness << YAML::Key << "ribbon_bevel" << YAML::Value << style.ribbonBevel << YAML::Key << "ribbon_bevel_segments" << YAML::Value << style.ribbonBevelSegments << YAML::Key << "ribbon_bevel_shape" << YAML::Value << style.ribbonBevelShape << YAML::Key << "shade_smooth" << YAML::Value << style.shadeSmooth << YAML::Key << "width" << YAML::Value << style.width << YAML::Key << "join" << YAML::Value << style.join << YAML::Key << "cap" << YAML::Value << style.cap << YAML::Key << "radialSegments" << YAML::Value << style.radialSegments;
			EmitVec3(emit, "color", style.color);
			emit << YAML::Key << "ribbon_bevel_parts" << YAML::Value << style.ribbonBevelParts;
			emit << YAML::Key << "alpha" << YAML::Value << style.alpha << YAML::Key << "dashEnabled" << YAML::Value << style.dashEnabled << YAML::Key << "dashLength" << YAML::Value << style.dashLength << YAML::Key << "gapLength" << YAML::Value << style.gapLength << YAML::Key << "dashPhase" << YAML::Value << style.dashPhase << YAML::Key << "gradientEnabled" << YAML::Value << style.gradientEnabled << YAML::Key << "gradientStops" << YAML::Value << YAML::BeginSeq;
			for (const auto &stop : style.gradientStops)
			{
				emit << YAML::BeginMap << YAML::Key << "position" << YAML::Value << stop.position;
				EmitVec3(emit, "color", stop.color);
				emit << YAML::Key << "alpha" << YAML::Value << stop.alpha << YAML::EndMap;
			}
			emit << YAML::EndSeq << YAML::Key << "startDecoration" << YAML::Value << style.startDecoration << YAML::Key << "startDecorationLengthScale" << YAML::Value << style.startDecorationLengthScale << YAML::Key << "startDecorationWidthScale" << YAML::Value << style.startDecorationWidthScale << YAML::Key << "start_decoration_filled" << YAML::Value << style.startDecorationFilled << YAML::Key << "endDecoration" << YAML::Value << style.endDecoration << YAML::Key << "endDecorationLengthScale" << YAML::Value << style.endDecorationLengthScale << YAML::Key << "endDecorationWidthScale" << YAML::Value << style.endDecorationWidthScale << YAML::Key << "end_decoration_filled" << YAML::Value << style.endDecorationFilled << YAML::Key << "depthMode" << YAML::Value << style.depthMode << YAML::EndMap;
		}
	}

	bool ParsePath(const YAML::Node &node, PersistedScenePath &path)
	{
		try
		{
			const YAML::Node nodes = node["nodes"];
			const YAML::Node segments = node["segments"];
			if (!nodes || !nodes.IsSequence() || nodes.size() < 2 || !segments || !segments.IsSequence() || segments.size() != nodes.size() - 1)
				return false;
			path.persistKey = node["persistKey"].as<std::string>(path.persistKey);
			path.name = node["name"].as<std::string>(path.name);
			path.visible = node["visible"].as<bool>(path.visible);
			path.renderable = node["renderable"].as<bool>(path.renderable);
			path.transformPosition = glm::vec3(0.0f);
			path.transformRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			path.transformScale = glm::vec3(1.0f);
			const bool hasTransform = node["transform_position"] || node["transform_rotation"] || node["transform_scale"];
			if (node["transform_position"] && !Vec3(node["transform_position"], path.transformPosition)) return false;
			if (node["transform_rotation"] && !Vec4(node["transform_rotation"], path.transformRotation)) return false;
			if (node["transform_scale"] && !Vec3(node["transform_scale"], path.transformScale)) return false;
			if (path.transformPosition.x == 0.0f)
				path.transformPosition.x = 0.0f;
			// PersistedScenePath intentionally has no presence bit in its contract. Keep the
			// identity values while using signed zero only as a parser-to-persistence marker:
			// omitted block = position.x -0, explicit identity block = rotation.x -0.
			if (!hasTransform)
				path.transformPosition.x = -0.0f;
			else if (path.transformPosition == glm::vec3(0.0f) &&
				path.transformRotation == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) &&
				path.transformScale == glm::vec3(1.0f))
				path.transformRotation.x = -0.0f;
			path.nodes.clear();
			for (const YAML::Node &nodeNode : nodes)
			{
				PersistedPathNode value;
				if (!nodeNode.IsMap() || !nodeNode["position"] || !Vec3(nodeNode["position"], value.position) || !ParseBinding(nodeNode["binding"], value.binding)) return false;
				path.nodes.push_back(std::move(value));
			}
			path.segments.clear();
			for (const YAML::Node &segmentNode : segments)
			{
				PersistedPathSegment value;
				if (!segmentNode.IsMap()) return false;
				const std::string kind = segmentNode["kind"].as<std::string>("Line");
				if (kind == "Line") value.kind = PersistedPathSegmentKind::Line;
				else if (kind == "Cubic") value.kind = PersistedPathSegmentKind::Cubic;
				else if (kind == "Arc") value.kind = PersistedPathSegmentKind::Arc;
				else return false;
				if (value.kind == PersistedPathSegmentKind::Cubic && ((!segmentNode["startHandle"] || !Vec3(segmentNode["startHandle"], value.startHandle)) || (!segmentNode["endHandle"] || !Vec3(segmentNode["endHandle"], value.endHandle)))) return false;
				value.startHandleType = segmentNode["startHandleType"].as<std::string>(value.startHandleType);
				value.endHandleType = segmentNode["endHandleType"].as<std::string>(value.endHandleType);
				if (value.kind == PersistedPathSegmentKind::Arc && (!segmentNode["planeNormal"] || !Vec3(segmentNode["planeNormal"], value.planeNormal))) return false;
				value.signedSweepRadians = segmentNode["signedSweepRadians"].as<float>(value.signedSweepRadians);
				path.segments.push_back(std::move(value));
			}
			return ParsePathStyle(node["style"], path.style);
		}
		catch (const YAML::Exception &)
		{
			return false;
		}
	}

	void EmitPath(YAML::Emitter &emit, const PersistedScenePath &path)
	{
		emit << YAML::Key << "kind" << YAML::Value << "ScenePath" << YAML::Key << "persistKey" << YAML::Value << path.persistKey << YAML::Key << "name" << YAML::Value << path.name;
		if (!std::signbit(path.transformPosition.x))
		{
			EmitVec3(emit, "transform_position", path.transformPosition);
			EmitVec4(emit, "transform_rotation", path.transformRotation);
			EmitVec3(emit, "transform_scale", path.transformScale);
		}
		emit << YAML::Key << "nodes" << YAML::Value << YAML::BeginSeq;
		for (const auto &node : path.nodes)
		{
			emit << YAML::BeginMap;
			EmitVec3(emit, "position", node.position);
			EmitBinding(emit, node.binding);
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::Key << "segments" << YAML::Value << YAML::BeginSeq;
		for (const auto &segment : path.segments)
		{
			emit << YAML::BeginMap << YAML::Key << "kind" << YAML::Value << (segment.kind == PersistedPathSegmentKind::Line ? "Line" : segment.kind == PersistedPathSegmentKind::Cubic ? "Cubic" : "Arc");
			if (segment.kind == PersistedPathSegmentKind::Cubic)
			{
				EmitVec3(emit, "startHandle", segment.startHandle);
				EmitVec3(emit, "endHandle", segment.endHandle);
				emit << YAML::Key << "startHandleType" << YAML::Value << segment.startHandleType << YAML::Key << "endHandleType" << YAML::Value << segment.endHandleType;
			}
			if (segment.kind == PersistedPathSegmentKind::Arc)
			{
				EmitVec3(emit, "planeNormal", segment.planeNormal);
				emit << YAML::Key << "signedSweepRadians" << YAML::Value << segment.signedSweepRadians;
			}
			emit << YAML::EndMap;
		}
		emit << YAML::EndSeq << YAML::Key << "visible" << YAML::Value << path.visible << YAML::Key << "renderable" << YAML::Value << path.renderable;
		EmitPathStyle(emit, path.style);
	}
} // namespace DefectStudio::SceneObjectsYaml
