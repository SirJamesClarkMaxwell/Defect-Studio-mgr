#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathBindingOperations.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <type_traits>
#include <utility>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransformPathElements.hpp"

namespace DefectStudio
{
	namespace
	{
		StructuredError BindingError(const char *code, const char *message)
		{
			return {ErrorCategory::Validation, Severity::Error, message, message,
				"Select a compatible path node and binding and try again.",
				"Presentation/ScenePathBindingOperations", code, DisplayPolicy::Silent};
		}

		Result<std::size_t> ActiveNodeIndex(const RendererWindowState &window)
		{
			if (window.pathEdit.IsActive() && window.paths != nullptr)
				if (const auto path = window.paths->Store().Find(window.pathEdit.Path()); path != nullptr)
					for (std::size_t index = 0; index < path->nodes.size(); ++index)
						if (path->nodes[index].id == window.pathEdit.ActiveElement())
							return index;
			return BindingError("path.edit_node_required", "Select an active node in Path Edit Mode.");
		}

		Result<void> ValidateObjectOrigin(const RendererWindowState &window, SceneObjectId id)
		{
			const BindingContext context = SceneSystem::MakePathBindingContext(window);
			if (context.isScenePath(id))
				return BindingError("path.object_origin_targets_path", "Object origin bindings may not target paths.");
			const auto origin = context.objectOrigin(id);
			const bool persisted = FindAnnotation(window.pinnedMeasurements, id) != nullptr || FindAnnotation(window.freeLabels, id) != nullptr ||
				FindAnnotation(window.sceneOrbitals, id) != nullptr ||
				FindAnnotation(window.scenePlanes, id) != nullptr;
			if (!id.IsValid() || !persisted || !origin || !std::isfinite(origin->x) || !std::isfinite(origin->y) || !std::isfinite(origin->z))
				return BindingError("path.binding_object_unresolved", "The bound object origin is unavailable.");
			return {};
		}
	} // namespace

	Result<PathBinding> ResolveActiveScenePathNodeBinding(const RendererWindowState &windowState)
	{
		const Result<std::size_t> index = ActiveNodeIndex(windowState);
		if (!index)
			return index.Error();
		return windowState.paths->Store().Find(windowState.pathEdit.Path())->nodes[index.Value()].binding;
	}

	Result<void> BindActiveScenePathNodeToSelectedAtoms(RendererWindowState &windowState)
	{
		const Result<std::size_t> index = ActiveNodeIndex(windowState);
		if (!index)
			return index.Error();
		const auto &atoms = windowState.selectedAtomIndices;
		if (atoms.size() != 1 && atoms.size() != 2)
			return BindingError("path.binding_atoms_required", "Select one or two atoms to bind the node.");
		if (atoms.size() == 1)
			return SetActiveScenePathNodeBinding(windowState, PathBinding{PathBinding::CopyPosition{atoms[0]}});
		return SetActiveScenePathNodeBinding(windowState, PathBinding{PathBinding::BondMidpoint{atoms[0], atoms[1]}});
	}

	Result<SceneObjectId> ResolveSelectedScenePathBindingObject(const RendererWindowState &windowState)
	{
		const Result<std::size_t> index = ActiveNodeIndex(windowState);
		if (!index)
			return index.Error();
		SceneObjectId target;
		std::size_t count = 0;
		for (const auto &selection : {std::cref(windowState.selectedPinnedMeasurements), std::cref(windowState.selectedFreeLabels),
			std::cref(windowState.selectedSceneOrbitals),
			std::cref(windowState.selectedScenePlanes), std::cref(windowState.selectedScenePaths)})
			for (const SceneObjectId id : selection.get())
				if (id != windowState.pathEdit.Path())
				{
					target = id;
					++count;
				}
		if (count != 1)
			return BindingError("path.binding_object_required", "Select exactly one other scene object to bind the node.");
		const Result<void> valid = ValidateObjectOrigin(windowState, target);
		return valid ? Result<SceneObjectId>{target} : Result<SceneObjectId>{valid.Error()};
	}

	Result<void> BindActiveScenePathNodeToSelectedObjectOrigin(RendererWindowState &windowState)
	{
		const Result<SceneObjectId> target = ResolveSelectedScenePathBindingObject(windowState);
		if (!target)
			return target.Error();
		return SetActiveScenePathNodeBinding(windowState, PathBinding{PathBinding::ObjectOrigin{target.Value()}});
	}

	Result<void> SetActiveScenePathNodeBinding(RendererWindowState &windowState, PathBinding binding)
	{
		const Result<std::size_t> index = ActiveNodeIndex(windowState);
		if (!index)
			return index.Error();
		const ScenePath &path = *windowState.paths->Store().Find(windowState.pathEdit.Path());
		const Result<void> valid = std::visit([&](const auto &value) -> Result<void> {
			using Binding = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<Binding, PathBinding::ObjectOrigin>)
				return ValidateObjectOrigin(windowState, value.object);
			else if constexpr (std::is_same_v<Binding, PathBinding::CopyPosition>)
			{
				if (value.atomIndex >= windowState.structure.atoms.size())
					return BindingError("path.binding_atom_out_of_range", "The bound atom is not in this structure.");
				if (value.buffer != 0.0f && index.Value() != 0 && index.Value() + 1 != path.nodes.size())
					return BindingError("path.binding_buffer_endpoint_only", "Only endpoint nodes support a binding buffer.");
				if (!std::isfinite(value.buffer))
					return BindingError("path.edit_invalid_result", "The binding buffer must be finite.");
			}
			else if constexpr (std::is_same_v<Binding, PathBinding::BondMidpoint>)
			{
				if (value.atomA >= windowState.structure.atoms.size() || value.atomB >= windowState.structure.atoms.size())
					return BindingError("path.binding_atom_out_of_range", "The bound atoms are not in this structure.");
			}
			return {};
		}, binding.value);
		if (!valid)
			return valid.Error();
		return SetScenePathBinding(MakeWindowPathEditContext(windowState), path.id,
			path.nodes[index.Value()].id, std::move(binding));
	}

	Result<void> DetachActiveScenePathNodeKeepingPosition(RendererWindowState &windowState)
	{
		const Result<std::size_t> index = ActiveNodeIndex(windowState);
		if (!index)
			return index.Error();
		const ScenePath &path = *windowState.paths->Store().Find(windowState.pathEdit.Path());
		if (std::holds_alternative<PathBinding::Free>(path.nodes[index.Value()].binding.value))
			return {};
		if (!HasInvertibleScenePathTransform(path))
			return BindingError("path.edit_invalid_result", "Cannot detach under a collapsed path transform.");

		const ResolvedNodes resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(windowState));
		const glm::vec3 local = ScenePathWorldToLocal(path, resolved.positions[index.Value()]);
		const std::array targets{path.id};
		const PathEditReport report = ApplyPathEdit(MakeWindowPathEditContext(windowState), targets,
			PathRevisionKind::Geometry, "Detach path node (keep position)", [&](ScenePath &edited) {
				PathNode &node = edited.nodes[index.Value()];
				node.position = local;
				node.binding = PathBinding{};
				return Result<void>{};
			});
		return report.AnyApplied() ? Result<void>{} : Result<void>(report.skipped.front().reason);
	}
} // namespace DefectStudio
