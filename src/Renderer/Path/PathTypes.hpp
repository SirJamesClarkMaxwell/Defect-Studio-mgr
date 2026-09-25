#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathStyle.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// Stable handle for one node, segment or handle inside ONE path. Monotonic per path and never
	// reused, so a stale id resolves to nothing. Regenerated on load - nothing outside the path may
	// reference an element (plan v2 C11).
	struct PathElementId
	{
		std::uint64_t value = 0; // 0 == unset/invalid; AllocateElementId hands out 1 and up

		[[nodiscard]] bool IsValid() const
		{
			return value != 0;
		}

		friend bool operator==(PathElementId, PathElementId) = default;
		friend auto operator<=>(PathElementId, PathElementId) = default;
	};

	enum class BezierHandleType
	{
		Free,
		Aligned,
		Vector,
		Auto,
	};

	struct PathHandle
	{
		PathElementId id;
		glm::vec3 position{0.0f}; // absolute world position, not an offset from the node
		BezierHandleType type = BezierHandleType::Auto;
	};

	// A small closed variant, deliberately not a constraint stack. Authored data only: resolving a
	// binding never writes back into the path (plan v2 C8).
	struct PathBinding
	{
		struct Free
		{
		};

		struct CopyPosition
		{
			std::size_t atomIndex = 0;
			glm::vec3 offset{0.0f};
			float buffer = 0.0f; // endpoint nodes only; interior use is a diagnostic
		};

		struct BondMidpoint
		{
			std::size_t atomA = 0;
			std::size_t atomB = 0;
			glm::vec3 offset{0.0f};
		};

		struct ObjectOrigin
		{
			SceneObjectId object;
			glm::vec3 offset{0.0f};
		};

		std::variant<Free, CopyPosition, BondMidpoint, ObjectOrigin> value = Free{};
	};

	struct PathNode
	{
		PathElementId id;
		glm::vec3 position{0.0f}; // authored position; also the frozen fallback when a binding breaks
		PathBinding binding;
	};

	struct LineSegmentData
	{
	};

	struct CubicBezierSegmentData
	{
		PathHandle startHandle;
		PathHandle endHandle;
	};

	// Nodes are the only source of truth: the endpoints come from the adjacent nodes, everything
	// else (center, radius, start angle) is derived (plan v2 C2).
	struct CircularArcSegmentData
	{
		glm::vec3 planeNormal{0.0f, 0.0f, 1.0f};
		float signedSweepRadians = 0.0f;
	};

	using PathSegmentData = std::variant<LineSegmentData, CubicBezierSegmentData, CircularArcSegmentData>;

	struct PathSegment
	{
		PathElementId id;
		PathSegmentData data;
	};

	struct ScenePath
	{
		SceneObjectId id;
		std::string persistKey;
		std::string name;
		std::vector<PathNode> nodes;
		std::vector<PathSegment> segments; // size == nodes.size() - 1 for a valid open path
		std::uint64_t nextElementId = 1;
		bool visible = true;
		bool renderable = true;
		// Style defaults produce a plain opaque round tube, so every pre-S5 construction of a
		// ScenePath keeps meaning exactly what it meant before.
		PathStrokeStyle style;
	};

	[[nodiscard]] inline PathElementId AllocateElementId(ScenePath &path)
	{
		return PathElementId{path.nextElementId++};
	}
}
