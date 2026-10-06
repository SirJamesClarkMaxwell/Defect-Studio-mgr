#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
		// task/41 transform-1: an OFFSET from the node this handle belongs to, in the path's local
		// space. It used to be an absolute position, and that stopped working the moment a path got
		// an object transform: a segment between a free node and a node bound to an atom has one
		// handle that should follow the transform and one that should follow the atom, and an
		// absolute handle has to be told which. An offset follows its own node either way, so
		// neither the transform nor the binding resolver has to ask.
		//
		// Files written before this store absolute positions; loading subtracts the node position
		// once. The conversion is lossless.
		glm::vec3 offset{0.0f};
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

		struct CopyVacancy
		{
			std::size_t vacancyIndex = 0;
			glm::vec3 offset{0.0f};
			float buffer = 0.0f; // endpoint nodes only, in vacancy marker radii
		};

		std::variant<Free, CopyPosition, BondMidpoint, ObjectOrigin, CopyVacancy> value = Free{};
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

	// Where a path sits, which way it faces and how big it is - the same three things Blender's
	// Object Mode gives every object, and the reason its N panel can show Location, Rotation and
	// Scale as persistent fields while Edit Mode cannot.
	//
	// task/41 transform-1. Before this, node positions were world space and there was no transform
	// at all, so the Properties panel had nothing to show, `SceneTransformLocalBasis` returned
	// nullopt for a path-only selection, and Local orientation silently fell back to Global.
	//
	// The rotation is stored as a quaternion and displayed as XYZ degrees. Storing the Euler angles
	// instead would make the panel simpler and the modal transform wrong: `R` rotates about an
	// arbitrary axis, and composing that onto Euler angles has to round-trip through a matrix and
	// pick one of several equivalent readings every time.
	struct PathTransform
	{
		glm::vec3 position{0.0f};
		glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
		glm::vec3 scale{1.0f};
	};

	// A whole-object binding, as opposed to the per-node PathBinding above. A ring drawn around a
	// bond cannot bind node by node - none of its nodes sit on an atom - so what follows the atoms
	// is the path's origin and orientation, not its vertices. That is the Blender Object Mode model
	// the paths already use: the nodes stay put in local space and the transform moves under them.
	struct PathTransformBinding
	{
		struct Free
		{
		};

		// Origin at the midpoint of atomA..atomB, local +z along the bond. Rotation about the bond
		// is free: `roll` is the user's rotation parameter, kept here so the binding can be
		// re-resolved without losing where the user put the arc.
		struct BondFrame
		{
			std::size_t atomA = 0;
			std::size_t atomB = 0;
			float rollRadians = 0.0f;
		};

		std::variant<Free, BondFrame> value = Free{};
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
		// The identity by default, and under the identity a local position equals the world position
		// it used to be - which is what makes every file written before this load unchanged and the
		// format version stay where it is.
		PathTransform transform;
		// Free by default, so every path built before this - and every file written before it -
		// keeps the transform it was given and loads unchanged.
		PathTransformBinding transformBinding;
	};

	[[nodiscard]] inline PathElementId AllocateElementId(ScenePath &path)
	{
		return PathElementId{path.nextElementId++};
	}
}
