#include "Core/dspch.hpp"

#include "Renderer/Scene/ScenePathPersistence.hpp"

#include <cmath>
#include <string>
#include <type_traits>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::vec3 &v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		StructuredError PathError(const std::string &detail)
		{
			return {ErrorCategory::Validation, Severity::Error, "Scene path could not be loaded", detail,
				"Correct the path data and try again.", "ScenePathPersistence", "scene_objects.path_invalid", DisplayPolicy::Silent};
		}

		void Warn(std::vector<StructuredError> &warnings, const std::string &detail)
		{
			warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path migration lost information", detail,
				"Review the migration report.", "ScenePathPersistence", "scene_objects.path_migration_lossy");
		}

		bool ParseHandleType(const std::string &name, BezierHandleType &out)
		{
			if (name == "Free") out = BezierHandleType::Free;
			else if (name == "Aligned") out = BezierHandleType::Aligned;
			else if (name == "Vector") out = BezierHandleType::Vector;
			else if (name == "Auto") out = BezierHandleType::Auto;
			else return false;
			return true;
		}

		const char *HandleTypeName(BezierHandleType type)
		{
			switch (type)
			{
				case BezierHandleType::Free: return "Free";
				case BezierHandleType::Aligned: return "Aligned";
				case BezierHandleType::Vector: return "Vector";
				case BezierHandleType::Auto: return "Auto";
			}
			return "Auto";
		}

		bool ParseStyle(const PersistedPathStyle &input, PathStrokeStyle &out)
		{
			if (input.profile == "Round") out.profile = StrokeProfile::Round;
			else if (input.profile == "Flat") out.profile = StrokeProfile::Flat;
			else if (input.profile == "CameraFacing") out.profile = StrokeProfile::CameraFacing;
			else return false;
			if (input.join == "Bevel") out.join = PathLineJoin::Bevel;
			else if (input.join == "Round") out.join = PathLineJoin::Round;
			else return false;
			if (input.cap == "Butt") out.cap = PathLineCap::Butt;
			else if (input.cap == "Square") out.cap = PathLineCap::Square;
			else if (input.cap == "Round") out.cap = PathLineCap::Round;
			else return false;
			if (input.depthMode == "DepthTest") out.depthMode = PathDepthMode::DepthTest;
			else if (input.depthMode == "AlwaysOnTop") out.depthMode = PathDepthMode::AlwaysOnTop;
			else return false;
			const auto decoration = [](const std::string &name, PathDecorationKind &kind) {
				if (name == "None") kind = PathDecorationKind::None;
				else if (name == "Arrow") kind = PathDecorationKind::Arrow;
				else if (name == "Stealth") kind = PathDecorationKind::Stealth;
				else if (name == "OpenArrow") kind = PathDecorationKind::OpenArrow;
				else if (name == "Bar") kind = PathDecorationKind::Bar;
				else if (name == "Circle") kind = PathDecorationKind::Circle;
				else if (name == "Square") kind = PathDecorationKind::Square;
				else if (name == "Diamond") kind = PathDecorationKind::Diamond;
				else return false;
				return true;
			};
			if (!decoration(input.startDecoration, out.startDecoration.kind) || !decoration(input.endDecoration, out.endDecoration.kind)) return false;
			out.width = input.width;
			if (Finite(input.ribbonNormal) && glm::length(input.ribbonNormal) > 1e-6f)
				out.ribbonNormal = input.ribbonNormal;
			out.radialSegments = input.radialSegments < 3 ? 3u : static_cast<std::uint32_t>(input.radialSegments);
			out.color = input.color;
			out.alpha = input.alpha;
			out.dash = {input.dashEnabled, input.dashLength, input.gapLength, input.dashPhase};
			out.gradient.enabled = input.gradientEnabled;
			out.gradient.stops.clear();
			for (const auto &stop : input.gradientStops)
				out.gradient.stops.push_back({stop.position, stop.color, stop.alpha});
			out.startDecoration.lengthScale = input.startDecorationLengthScale;
			out.startDecoration.widthScale = input.startDecorationWidthScale;
			out.endDecoration.lengthScale = input.endDecorationLengthScale;
			out.endDecoration.widthScale = input.endDecorationWidthScale;
			return true;
		}

		PersistedAtomRef AtomRef(const RendererStructureData &structure, std::size_t index)
		{
			PersistedAtomRef result;
			result.index = index;
			if (index < structure.atoms.size())
			{
				result.element = structure.atoms[index].element;
				result.position = structure.atoms[index].cartesianPosition;
			}
			return result;
		}

		PathBinding BuildBinding(const PersistedPathBinding &input, const RendererStructureData &structure,
			const glm::vec3 &position, std::vector<StructuredError> &warnings)
		{
			if (input.kind == "CopyPosition" && input.atoms.size() == 1)
			{
				const auto index = ResolveAtomReference(structure, input.atoms.front());
				if (index) return PathBinding{PathBinding::CopyPosition{*index, input.offset, input.buffer}};
				warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path binding is unresolved", "CopyPosition atom reference could not be resolved.", "The node keeps its stored position and is treated as free.", "ScenePathPersistence", "scene_objects.path_binding_unresolved");
			}
			else if (input.kind == "BondMidpoint" && input.atoms.size() == 2)
			{
				const auto first = ResolveAtomReference(structure, input.atoms[0]);
				const auto second = ResolveAtomReference(structure, input.atoms[1]);
				if (first && second) return PathBinding{PathBinding::BondMidpoint{*first, *second, input.offset}};
				warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path binding is unresolved", "BondMidpoint atom references could not be resolved.", "The node keeps its stored position and is treated as free.", "ScenePathPersistence", "scene_objects.path_binding_unresolved");
			}
			else if (input.kind == "ObjectOrigin")
			{
				warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene path binding is unresolved", "ObjectOrigin bindings require a second scene-object resolution pass.", "The node keeps its stored position and is treated as free.", "ScenePathPersistence", "scene_objects.path_binding_unresolved");
			}
			(void)position;
			return PathBinding{PathBinding::Free{}};
		}
	}

	Result<ScenePath> BuildScenePath(const PersistedScenePath &persisted, const RendererStructureData &structure,
		std::vector<StructuredError> &outWarnings)
	{
		if (persisted.nodes.size() < 2 || persisted.segments.size() != persisted.nodes.size() - 1)
			return PathError("A scene path requires at least two nodes and exactly one segment per node gap.");
		ScenePath path;
		path.persistKey = persisted.persistKey;
		path.name = persisted.name;
		path.visible = persisted.visible;
		path.renderable = persisted.renderable;
		if (!ParseStyle(persisted.style, path.style))
			return PathError("Scene path contains an unknown style enum name.");
		for (const auto &node : persisted.nodes)
		{
			if (!Finite(node.position)) return PathError("Scene path contains a non-finite node position.");
			PathNode built;
			built.id = AllocateElementId(path);
			built.position = node.position;
			built.binding = BuildBinding(node.binding, structure, node.position, outWarnings);
			path.nodes.push_back(std::move(built));
		}
		for (const auto &segment : persisted.segments)
		{
			PathSegment built;
			built.id = AllocateElementId(path);
			if (segment.kind == PersistedPathSegmentKind::Line)
				built.data = LineSegmentData{};
			else if (segment.kind == PersistedPathSegmentKind::Cubic)
			{
				BezierHandleType startType, endType;
				if (!Finite(segment.startHandle) || !Finite(segment.endHandle) || !ParseHandleType(segment.startHandleType, startType) || !ParseHandleType(segment.endHandleType, endType)) return PathError("Scene path contains invalid cubic handle data.");
				built.data = CubicBezierSegmentData{{AllocateElementId(path), segment.startHandle, startType}, {AllocateElementId(path), segment.endHandle, endType}};
			}
			else
			{
				if (!Finite(segment.planeNormal) || !std::isfinite(segment.signedSweepRadians)) return PathError("Scene path contains invalid arc data.");
				built.data = CircularArcSegmentData{segment.planeNormal, segment.signedSweepRadians};
			}
			path.segments.push_back(std::move(built));
		}
		if (!ValidatePath(path).empty()) return PathError("Scene path geometry violates renderer path invariants.");
		return path;
	}

	PersistedScenePath ExtractPersistedScenePath(const ScenePath &path, const RendererStructureData &structure)
	{
		PersistedScenePath persisted;
		persisted.persistKey = path.persistKey;
		persisted.name = path.name;
		persisted.visible = path.visible;
		persisted.renderable = path.renderable;
		persisted.nodes.reserve(path.nodes.size());
		for (const auto &node : path.nodes)
		{
			PersistedPathNode saved;
			saved.position = node.position;
			std::visit([&](const auto &binding) {
				using Binding = std::decay_t<decltype(binding)>;
				if constexpr (std::is_same_v<Binding, PathBinding::CopyPosition>)
				{
					saved.binding.kind = "CopyPosition";
					saved.binding.atoms = {AtomRef(structure, binding.atomIndex)};
					saved.binding.offset = binding.offset;
					saved.binding.buffer = binding.buffer;
				}
				else if constexpr (std::is_same_v<Binding, PathBinding::BondMidpoint>)
				{
					saved.binding.kind = "BondMidpoint";
					saved.binding.atoms = {AtomRef(structure, binding.atomA), AtomRef(structure, binding.atomB)};
					saved.binding.offset = binding.offset;
				}
				else if constexpr (std::is_same_v<Binding, PathBinding::ObjectOrigin>)
				{
					saved.binding.kind = "ObjectOrigin";
					saved.binding.offset = binding.offset;
				}
			}, node.binding.value);
			persisted.nodes.push_back(std::move(saved));
		}
		persisted.segments.reserve(path.segments.size());
		for (const auto &segment : path.segments)
		{
			PersistedPathSegment saved;
			std::visit([&](const auto &data) {
				using Data = std::decay_t<decltype(data)>;
				if constexpr (std::is_same_v<Data, CubicBezierSegmentData>)
				{
					saved.kind = PersistedPathSegmentKind::Cubic;
					saved.startHandle = data.startHandle.position;
					saved.endHandle = data.endHandle.position;
					saved.startHandleType = HandleTypeName(data.startHandle.type);
					saved.endHandleType = HandleTypeName(data.endHandle.type);
				}
				else if constexpr (std::is_same_v<Data, CircularArcSegmentData>)
				{
					saved.kind = PersistedPathSegmentKind::Arc;
					saved.planeNormal = data.planeNormal;
					saved.signedSweepRadians = data.signedSweepRadians;
				}
			}, segment.data);
			persisted.segments.push_back(std::move(saved));
		}
		persisted.style.profile = path.style.profile == StrokeProfile::Round ? "Round" : path.style.profile == StrokeProfile::Flat ? "Flat" : "CameraFacing";
		persisted.style.ribbonNormal = path.style.ribbonNormal;
		persisted.style.width = path.style.width;
		persisted.style.join = path.style.join == PathLineJoin::Bevel ? "Bevel" : "Round";
		persisted.style.cap = path.style.cap == PathLineCap::Butt ? "Butt" : path.style.cap == PathLineCap::Square ? "Square" : "Round";
		persisted.style.radialSegments = static_cast<int>(path.style.radialSegments);
		persisted.style.color = path.style.color;
		persisted.style.alpha = path.style.alpha;
		persisted.style.dashEnabled = path.style.dash.enabled;
		persisted.style.dashLength = path.style.dash.dashLength;
		persisted.style.gapLength = path.style.dash.gapLength;
		persisted.style.dashPhase = path.style.dash.phase;
		persisted.style.gradientEnabled = path.style.gradient.enabled;
		for (const auto &stop : path.style.gradient.stops) persisted.style.gradientStops.push_back({stop.position, stop.color, stop.alpha});
		const auto decoration = [](const PathEndpointDecoration &value, std::string &kind, float &length, float &width) {
			const char *names[] = {"None", "Arrow", "Stealth", "OpenArrow", "Bar", "Circle", "Square", "Diamond"};
			kind = names[static_cast<int>(value.kind)]; length = value.lengthScale; width = value.widthScale;
		};
		decoration(path.style.startDecoration, persisted.style.startDecoration, persisted.style.startDecorationLengthScale, persisted.style.startDecorationWidthScale);
		decoration(path.style.endDecoration, persisted.style.endDecoration, persisted.style.endDecorationLengthScale, persisted.style.endDecorationWidthScale);
		persisted.style.depthMode = path.style.depthMode == PathDepthMode::DepthTest ? "DepthTest" : "AlwaysOnTop";
		return persisted;
	}

	Result<ScenePathMigration> MigrateArrowToPath(const PersistedSceneArrow &arrow)
	{
		if (arrow.points.size() < 2) return PathError("A v1 arrow requires at least two points.");
		for (const auto &point : arrow.points) if (!Finite(point)) return PathError("A v1 arrow contains a non-finite coordinate.");
		if (arrow.controlPoint && !Finite(*arrow.controlPoint)) return PathError("A v1 arrow contains a non-finite coordinate.");
		ScenePathMigration result;
		result.path.persistKey = arrow.persistKey;
		result.path.nodes.reserve(arrow.points.size());
		for (const auto &point : arrow.points) result.path.nodes.push_back({AllocateElementId(result.path), point, {}});
		const bool cubic = arrow.points.size() == 2 && arrow.controlPoint.has_value();
		if (arrow.controlPoint && !cubic) Warn(result.warnings, "curve control point on a multi-point arrow was dropped");
		for (std::size_t index = 0; index + 1 < arrow.points.size(); ++index)
		{
			PathSegment segment;
			segment.id = AllocateElementId(result.path);
			if (cubic)
			{
				const glm::vec3 c1 = arrow.points[0] + (2.0f / 3.0f) * (*arrow.controlPoint - arrow.points[0]);
				const glm::vec3 c2 = arrow.points[1] + (2.0f / 3.0f) * (*arrow.controlPoint - arrow.points[1]);
				segment.data = CubicBezierSegmentData{{AllocateElementId(result.path), c1, BezierHandleType::Free}, {AllocateElementId(result.path), c2, BezierHandleType::Free}};
			}
			else segment.data = LineSegmentData{};
			result.path.segments.push_back(std::move(segment));
		}
		result.path.style.width = 2.0f * arrow.style.shaftWidth;
		result.path.style.profile = arrow.kind == PersistedArrowKind::Arrow2D ? (arrow.orientation2D == PersistedArrow2DOrientation::Billboard ? StrokeProfile::CameraFacing : StrokeProfile::Flat) : StrokeProfile::Round;
		result.path.style.depthMode = arrow.kind == PersistedArrowKind::Arrow2D ? PathDepthMode::AlwaysOnTop : PathDepthMode::DepthTest;
		if (arrow.kind == PersistedArrowKind::Arrow2D && arrow.orientation2D == PersistedArrow2DOrientation::FixedPlane) Warn(result.warnings, "Arrow2D FixedPlane world plane was dropped; the path uses its transported frame");
		result.path.style.color = arrow.style.color;
		result.path.style.alpha = arrow.style.alpha;
		result.path.style.dash = {arrow.style.dashed, arrow.style.dashLength, arrow.style.gapLength, 0.0f};
		result.path.style.gradient.enabled = arrow.style.useGradient;
		if (arrow.style.useGradient) result.path.style.gradient.stops = {{0.0f, arrow.style.gradientStart, arrow.style.alpha}, {1.0f, arrow.style.gradientFinish, arrow.style.alpha}};
		const auto tip = [](const std::string &name) {
			if (name == "Plain") return PathDecorationKind::Arrow;
			if (name == "Barbed") return PathDecorationKind::Stealth;
			if (name == "Open") return PathDecorationKind::OpenArrow;
			if (name == "Bar") return PathDecorationKind::Bar;
			if (name == "Circle") return PathDecorationKind::Circle;
			return PathDecorationKind::None;
		};
		result.path.style.startDecoration.kind = tip(arrow.startTip);
		result.path.style.endDecoration.kind = tip(arrow.endTip);
		// Both ends share the v1 head size, but not the v1 tip - assigning the whole struct would
		// overwrite the end tip that was just mapped.
		const float headLengthScale = result.path.style.width == 0.0f ? 0.0f : arrow.style.headLength / result.path.style.width;
		const float headWidthScale = result.path.style.width == 0.0f ? 0.0f : arrow.style.headWidth / result.path.style.width;
		result.path.style.startDecoration.lengthScale = headLengthScale;
		result.path.style.startDecoration.widthScale = headWidthScale;
		result.path.style.endDecoration.lengthScale = headLengthScale;
		result.path.style.endDecoration.widthScale = headWidthScale;
		if (!arrow.startAnchorAtoms.empty()) result.path.nodes.front().binding = PathBinding{PathBinding::CopyPosition{arrow.startAnchorAtoms.front().index, {}, arrow.atomBuffer}};
		if (!arrow.endAnchorAtoms.empty()) result.path.nodes.back().binding = PathBinding{PathBinding::CopyPosition{arrow.endAnchorAtoms.front().index, {}, arrow.atomBuffer}};
		if (arrow.curveSegments != 24) Warn(result.warnings, "curveSegments was dropped; path tessellation is adaptive");
		if (arrow.style.outlineWidth != 0.0f) Warn(result.warnings, "outlineWidth was dropped; paths have no per-object outline");
		return result;
	}
}
