#include "Core/dspch.hpp"

#include "Renderer/Path/PathBindingResolver.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool IsFinite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		void AddDiagnostic(ResolvedNodes &resolved, PathDiagnosticCode code, PathElementId element, const char *message)
		{
			resolved.diagnostics.push_back({code, element, message});
		}

		[[nodiscard]] glm::vec3 ResolveUnbuffered(
			const PathNode &node, const BindingContext &context, ResolvedNodes &resolved)
		{
			return std::visit([&](const auto &binding) -> glm::vec3 {
				using Binding = std::decay_t<decltype(binding)>;
				if constexpr (std::is_same_v<Binding, PathBinding::Free>)
					return node.position;
				else if constexpr (std::is_same_v<Binding, PathBinding::CopyPosition>)
				{
					if (context.atomPosition)
						if (const std::optional<glm::vec3> atom = context.atomPosition(binding.atomIndex); atom && IsFinite(*atom))
							return *atom + binding.offset;
					AddDiagnostic(resolved, PathDiagnosticCode::BrokenBinding, node.id, "Bound atom is unavailable.");
					return node.position;
				}
				else if constexpr (std::is_same_v<Binding, PathBinding::BondMidpoint>)
				{
					if (context.atomPosition)
					{
						const std::optional<glm::vec3> a = context.atomPosition(binding.atomA);
						const std::optional<glm::vec3> b = context.atomPosition(binding.atomB);
						if (a && b && IsFinite(*a) && IsFinite(*b))
							return (*a + *b) * 0.5f + binding.offset;
					}
					AddDiagnostic(resolved, PathDiagnosticCode::BrokenBinding, node.id, "Bound atoms are unavailable.");
					return node.position;
				}
				else
				{
					if (context.isScenePath && context.isScenePath(binding.object))
					{
						AddDiagnostic(resolved, PathDiagnosticCode::ObjectOriginTargetsPath, node.id, "Object origin bindings may not target paths.");
						return node.position;
					}
					if (context.objectOrigin)
						if (const std::optional<glm::vec3> origin = context.objectOrigin(binding.object); origin && IsFinite(*origin))
							return *origin + binding.offset;
					AddDiagnostic(resolved, PathDiagnosticCode::BrokenBinding, node.id, "Bound object origin is unavailable.");
					return node.position;
				}
			}, node.binding.value);
		}
	} // namespace

	ResolvedNodes ResolveNodePositions(const ScenePath &path, const BindingContext &context)
	{
		ResolvedNodes resolved;
		resolved.positions.reserve(path.nodes.size());
		for (const PathNode &node : path.nodes)
			resolved.positions.push_back(ResolveUnbuffered(node, context, resolved));

		if (path.nodes.size() < 2)
			return resolved;
		for (std::size_t index = 0; index < path.nodes.size(); ++index)
		{
			const auto *binding = std::get_if<PathBinding::CopyPosition>(&path.nodes[index].binding.value);
			if (binding == nullptr || binding->buffer == 0.0f)
				continue;
			if (index != 0 && index + 1 != path.nodes.size())
			{
				AddDiagnostic(resolved, PathDiagnosticCode::InteriorNodeBuffer, path.nodes[index].id, "Only endpoint CopyPosition bindings support a buffer.");
				continue;
			}
			if (binding->buffer <= 0.0f || !context.atomRadius)
				continue;
			const std::optional<float> radius = context.atomRadius(binding->atomIndex);
			if (!radius || !std::isfinite(*radius) || *radius <= 0.0f)
				continue;
			const std::size_t neighbourIndex = index == 0 ? 1 : index - 1;
			const glm::vec3 delta = resolved.positions[neighbourIndex] - resolved.positions[index];
			const float distance = glm::length(delta);
			const float requested = binding->buffer * *radius;
			if (!std::isfinite(distance) || distance <= 1.0e-4f || requested <= 1.0e-4f)
				continue;
			// Mirror SceneSystem's non-inversion clamp: retain ten percent of the original direction.
			const float scale = std::min(1.0f, 0.9f * distance / requested);
			resolved.positions[index] += delta / distance * (requested * scale);
		}
		return resolved;
	}
}
