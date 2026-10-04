#include "Core/dspch.hpp"

#include "Renderer/Path/PathBindingResolver.hpp"

#include <algorithm>
#include <bit>
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

		[[nodiscard]] glm::vec3 TransformLocalPosition(const ScenePath &path, const glm::vec3 &position)
		{
			return path.transform.position + path.transform.rotation * (path.transform.scale * position);
		}

		[[nodiscard]] glm::vec3 TransformHandleOffset(const ScenePath &path, const glm::vec3 &offset)
		{
			return path.transform.rotation * (path.transform.scale * offset);
		}

		[[nodiscard]] glm::vec3 ResolveUnbuffered(
			const ScenePath &path, const PathNode &node, const BindingContext &context, ResolvedNodes &resolved)
		{
			return std::visit([&](const auto &binding) -> glm::vec3 {
				using Binding = std::decay_t<decltype(binding)>;
				if constexpr (std::is_same_v<Binding, PathBinding::Free>)
					return TransformLocalPosition(path, node.position);
				else if constexpr (std::is_same_v<Binding, PathBinding::CopyPosition>)
				{
					if (context.atomPosition)
						if (const std::optional<glm::vec3> atom = context.atomPosition(binding.atomIndex); atom && IsFinite(*atom))
							return *atom + binding.offset;
					AddDiagnostic(resolved, PathDiagnosticCode::BrokenBinding, node.id, "Bound atom is unavailable.");
					return TransformLocalPosition(path, node.position);
				}
				else if constexpr (std::is_same_v<Binding, PathBinding::CopyVacancy>)
				{
					if (context.vacancyPosition)
						if (const auto vacancy = context.vacancyPosition(binding.vacancyIndex); vacancy && IsFinite(*vacancy))
							return *vacancy + binding.offset;
					AddDiagnostic(resolved, PathDiagnosticCode::BrokenBinding, node.id, "Bound vacancy is unavailable.");
					return TransformLocalPosition(path, node.position);
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
					return TransformLocalPosition(path, node.position);
				}
				else
				{
					if (context.isScenePath && context.isScenePath(binding.object))
					{
						AddDiagnostic(resolved, PathDiagnosticCode::ObjectOriginTargetsPath, node.id, "Object origin bindings may not target paths.");
						return TransformLocalPosition(path, node.position);
					}
					if (context.objectOrigin)
						if (const std::optional<glm::vec3> origin = context.objectOrigin(binding.object); origin && IsFinite(*origin))
							return *origin + binding.offset;
					AddDiagnostic(resolved, PathDiagnosticCode::BrokenBinding, node.id, "Bound object origin is unavailable.");
					return TransformLocalPosition(path, node.position);
				}
			}, node.binding.value);
		}
	} // namespace

	ResolvedNodes ResolveNodePositions(const ScenePath &path, const BindingContext &context)
	{
		ResolvedNodes resolved;
		resolved.positions.reserve(path.nodes.size());
		for (const PathNode &node : path.nodes)
			resolved.positions.push_back(ResolveUnbuffered(path, node, context, resolved));

		if (path.nodes.size() < 2)
			return resolved;
		const auto unbufferedPositions = resolved.positions;
		for (std::size_t index = 0; index < path.nodes.size(); ++index)
		{
			float buffer = 0.0f;
			std::optional<float> radius;
			std::visit([&](const auto &binding) {
				using Binding = std::decay_t<decltype(binding)>;
				if constexpr (std::is_same_v<Binding, PathBinding::CopyPosition>)
				{
					buffer = binding.buffer;
					if (buffer > 0.0f && context.atomRadius) radius = context.atomRadius(binding.atomIndex);
				}
				else if constexpr (std::is_same_v<Binding, PathBinding::CopyVacancy>)
				{
					buffer = binding.buffer;
					if (buffer > 0.0f && context.vacancyRadius) radius = context.vacancyRadius(binding.vacancyIndex);
				}
			}, path.nodes[index].binding.value);
			if (buffer == 0.0f)
				continue;
			if (index != 0 && index + 1 != path.nodes.size())
			{
				AddDiagnostic(resolved, PathDiagnosticCode::InteriorNodeBuffer, path.nodes[index].id, "Only endpoint position bindings support a buffer.");
				continue;
			}
			if (std::any_of(resolved.diagnostics.begin(), resolved.diagnostics.end(), [&](const PathDiagnostic &diagnostic) {
				return diagnostic.element == path.nodes[index].id && diagnostic.code == PathDiagnosticCode::BrokenBinding;
			}))
				continue;
			if (!std::isfinite(buffer) || buffer <= 0.0f || !radius || !std::isfinite(*radius) || *radius <= 0.0f)
				continue;
			const std::size_t neighbourIndex = index == 0 ? 1 : index - 1;
			const glm::vec3 delta = unbufferedPositions[neighbourIndex] - unbufferedPositions[index];
			const float distance = glm::length(delta);
			const float requested = buffer * *radius;
			if (!std::isfinite(distance) || !std::isfinite(requested) || distance <= 1.0e-4f || requested <= 1.0e-4f)
				continue;
			// Keep the existing atom non-inversion clamp. If the neighbour was buffered too,
			// retain ten percent of the REMAINING gap, while using the unbuffered direction.
			const float availableDistance = glm::length(resolved.positions[neighbourIndex] - resolved.positions[index]);
			const float scale = std::min(1.0f, 0.9f * availableDistance / requested);
			resolved.positions[index] += delta / distance * (requested * scale);
		}

		resolved.handlePositions.reserve(path.segments.size() * 2);
		for (std::size_t index = 0; index < path.segments.size(); ++index)
		{
			const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
			if (cubic == nullptr || index >= resolved.positions.size() || index + 1 >= resolved.positions.size())
			{
				resolved.handlePositions.push_back(glm::vec3(0.0f));
				resolved.handlePositions.push_back(glm::vec3(0.0f));
				continue;
			}
			resolved.handlePositions.push_back(
				resolved.positions[index] + TransformHandleOffset(path, cubic->startHandle.offset));
			resolved.handlePositions.push_back(
				resolved.positions[index + 1] + TransformHandleOffset(path, cubic->endHandle.offset));
		}
		return resolved;
	}

	std::uint64_t BindingSourceRevision(const ScenePath &path, const ResolvedNodes &resolved)
	{
		std::uint64_t hash = 14695981039346656037ull;
		bool hasBindings = false;
		for (std::size_t index = 0; index < path.nodes.size(); ++index)
		{
			if (std::holds_alternative<PathBinding::Free>(path.nodes[index].binding.value))
				continue;
			hasBindings = true;
			const glm::vec3 &position = resolved.positions[index];
			// FNV-1a over the float bit patterns, in node and x/y/z order. Hash components rather
			// than vec3 storage so padding and alignment cannot affect the revision.
			for (const float component : {position.x, position.y, position.z})
			{
				const std::uint32_t bits = std::bit_cast<std::uint32_t>(component);
				for (unsigned int shift = 0; shift < 32; shift += 8)
				{
					hash ^= (bits >> shift) & 0xffu;
					hash *= 1099511628211ull;
				}
			}
		}
		return hasBindings ? (hash == 0 ? 1 : hash) : 0;
	}
}
