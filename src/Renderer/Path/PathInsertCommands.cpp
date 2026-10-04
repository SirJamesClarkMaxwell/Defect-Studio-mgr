#include "Core/dspch.hpp"

#include "Renderer/Path/PathCommands.hpp"

#include <cmath>
#include <algorithm>
#include <utility>
#include <glm/gtc/quaternion.hpp>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	Result<std::vector<PathElementId>> InsertScenePathNodes(const PathEditContext &context,
		const SceneObjectId path, const std::size_t segment, const std::size_t count)
	{
		std::vector<PathElementId> inserted;
		const auto report = ApplyPathEdit(context, std::span<const SceneObjectId>(&path, 1),
			PathRevisionKind::Geometry, "Insert path nodes", [&](ScenePath &edited) -> Result<void> {
				// Split in evaluation space, then convert the new nodes/handles back to authored
				// space. Arcs are evaluated as world-space circles even under nonuniform scale.
				const auto scale = edited.transform.scale;
				if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) ||
					std::abs(scale.x) < 1.0e-8f || std::abs(scale.y) < 1.0e-8f || std::abs(scale.z) < 1.0e-8f)
					return StructuredError{ErrorCategory::Validation, Severity::Error, "Cannot split this path.",
						"Path scale is singular.", "Use a nonzero scale.", "Renderer/Path", "path.insert_singular_scale"};
				const auto bindings = SceneSystem::MakePathBindingContext(*context.window);
				const auto resolved = ResolveNodePositions(edited, bindings);
				for (std::size_t index = 0; index < edited.nodes.size(); ++index)
					edited.nodes[index].position = resolved.positions[index];
				for (std::size_t index = 0; index < edited.segments.size(); ++index)
					if (auto cubic = std::get_if<CubicBezierSegmentData>(&edited.segments[index].data))
					{
						cubic->startHandle.offset = resolved.handlePositions[index * 2] - resolved.positions[index];
						cubic->endHandle.offset = resolved.handlePositions[index * 2 + 1] - resolved.positions[index + 1];
					}
				const auto result = InsertNodes(edited, segment, count);
				if (!result)
					return result.Error();
				inserted = *result;
				// The endpoint buffer follows its neighbour. Splitting a curve changes that
				// direction (and can trigger the 90% clamp), so solve the offset for the same
				// displayed endpoint while retaining its atom binding and buffer setting.
				for (const auto endpoint : {std::size_t{0}, edited.nodes.size() - 1})
				{
					auto binding = std::get_if<PathBinding::CopyPosition>(&edited.nodes[endpoint].binding.value);
					if (!binding || binding->buffer <= 0) continue;
					const auto atom = bindings.atomPosition(binding->atomIndex);
					const auto radius = bindings.atomRadius(binding->atomIndex);
					if (!atom || !radius || *radius <= 0) continue;
					const auto neighbour = endpoint == 0 ? 1 : endpoint - 1;
					const auto position = edited.nodes[endpoint].position;
					const auto delta = edited.nodes[neighbour].position - position;
					const float distance = glm::length(delta);
					const float requested = binding->buffer * *radius;
					if (requested > 1.0e-4f)
					{
						binding->offset = position - *atom;
						if (distance > 1.0e-5f)
							binding->offset -= delta / distance * std::min(requested, 9.0f * distance);
					}
				}
				const auto inverseRotation = glm::inverse(edited.transform.rotation);
				for (auto &node : edited.nodes)
					node.position = (inverseRotation * (node.position - edited.transform.position)) / scale;
				for (auto &part : edited.segments)
					if (auto cubic = std::get_if<CubicBezierSegmentData>(&part.data))
					{
						cubic->startHandle.offset = (inverseRotation * cubic->startHandle.offset) / scale;
						cubic->endHandle.offset = (inverseRotation * cubic->endHandle.offset) / scale;
					}
				return {};
			});
		return report.AnyApplied() ? Result<std::vector<PathElementId>>{std::move(inserted)}
			: Result<std::vector<PathElementId>>{report.skipped.front().reason};
	}
}
