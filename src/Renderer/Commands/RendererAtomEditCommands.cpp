#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererAtomEditCommands.hpp"

#include <algorithm>
#include <utility>

#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
#include "Domain/Defects/DefectModel.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeAtomEditError(std::string code, std::string userMessage, std::string technicalDetails)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				std::move(userMessage),
				std::move(technicalDetails),
				"Select at least one atom in a viewport with an open structure before running this command.",
				"RendererAtomEditCommands",
				std::move(code)};
		}

		struct AtomEditTarget
		{
			RendererWindowState *windowState = nullptr;
			Ref<StructureRecord> record;
		};

		// Resolves windowId (or the focused viewport if empty) to its live window state and the
		// mutable domain StructureRecord backing it. Shared by every atom-edit command's Execute
		// and Undo, since both need to re-resolve rather than cache raw pointers across calls.
		[[nodiscard]] Result<AtomEditTarget> ResolveAtomEditTarget(
			RendererLayer &rendererLayer, DomainLayer &domainLayer, const std::string &windowIdParam)
		{
			const std::string windowId = windowIdParam.empty() ? rendererLayer.GetFocusedViewportWindowId() : windowIdParam;
			if (windowId.empty())
			{
				return MakeAtomEditError(
					"renderer.atom_edit.no_window",
					"No renderer viewport is focused.",
					"windowId was empty and no viewport is currently focused.");
			}

			RendererWindowState *windowState = nullptr;
			for (RendererWindowState &candidate : rendererLayer.GetWindows())
			{
				if (candidate.windowId == windowId)
				{
					windowState = &candidate;
					break;
				}
			}
			if (windowState == nullptr)
			{
				return MakeAtomEditError(
					"renderer.atom_edit.window_not_found",
					"Renderer window not found.",
					"windowId '" + windowId + "' does not match any open window.");
			}

			if (windowState->structure.domainStructureId.empty())
			{
				return MakeAtomEditError(
					"renderer.atom_edit.no_domain_structure",
					"This window has no editable structure.",
					"RendererStructureData::domainStructureId is empty - the structure was not registered "
					"in ProjectWorkspace.");
			}

			const std::optional<Uuid> structureId = ParseUuid(windowState->structure.domainStructureId);
			if (!structureId.has_value())
			{
				return MakeAtomEditError(
					"renderer.atom_edit.bad_structure_id",
					"This window's structure id is invalid.",
					"domainStructureId '" + windowState->structure.domainStructureId + "' is not a valid UUID.");
			}

			Ref<StructureRecord> record = domainLayer.Workspace().Structures().FindMutable(*structureId).lock();
			if (record == nullptr)
			{
				return MakeAtomEditError(
					"renderer.atom_edit.record_not_found",
					"This window's structure is no longer registered.",
					"No StructureRecord found for id " + windowState->structure.domainStructureId + ".");
			}

			return AtomEditTarget{windowState, std::move(record)};
		}

		// Rebuilds RendererStructureData from record.structure and re-syncs windowState's ECS
		// scene - the common tail every atom-edit command needs after mutating the domain
		// structure. selectAfter (atom indices in the *new* structure) becomes the selection once
		// synced; empty leaves nothing selected.
		void RebuildAndSync(
			RendererWindowState &windowState,
			const StructureRecord &record,
			const AtomStyleTable &atomStyleTable,
			const std::vector<std::size_t> &selectAfter)
		{
			windowState.structure = BuildRendererStructureData(
				record.structure,
				windowState.structure.sourcePath,
				windowState.structure.name,
				atomStyleTable,
				windowState.structure.domainStructureId);
			SceneSystem::SyncSceneWithStructure(windowState.sceneRegistry, windowState.structure);
			if (!selectAfter.empty())
				SceneSystem::ApplySelectionAndVisibilityToScene(windowState.sceneRegistry, selectAfter, {});
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}

		class DeleteSelectedAtomsCommand final : public ICommand
		{
		public:
			DeleteSelectedAtomsCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				ElementPropertiesTable elementPropertiesTable,
				std::string windowId)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
				  m_WindowId(std::move(windowId))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"DeleteSelectedAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				if (windowState.selectedAtomIndices.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.nothing_selected",
						"No atoms selected.",
						"DeleteSelectedAtomsCommand: selectedAtomIndices is empty.");
				}

				m_WindowIdResolved = windowState.windowId;
				m_PreviousAtoms = target->record->structure.atoms;
				m_PreviousBonds = target->record->structure.bonds;
				m_DeletedIndices = windowState.selectedAtomIndices;
				std::sort(m_DeletedIndices.begin(), m_DeletedIndices.end());

				// ApplyVacancy already strips/reindexes referencing bonds incrementally - highest
				// index first so earlier indices in the same batch stay valid as we go.
				for (auto it = m_DeletedIndices.rbegin(); it != m_DeletedIndices.rend(); ++it)
				{
					PointDefectOperation operation;
					operation.atomIndex = *it;
					Result<void> result = ApplyVacancy(target->record->structure, operation);
					if (!result)
						return result.Error();
				}

				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, {});
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"DeleteSelectedAtomsCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.atoms = m_PreviousAtoms;
				target->record->structure.bonds = m_PreviousBonds;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, m_DeletedIndices);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Delete selected atoms";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_AtomStyleTable;
			ElementPropertiesTable m_ElementPropertiesTable;
			std::string m_WindowId;
			std::string m_WindowIdResolved;
			std::vector<AtomSite> m_PreviousAtoms;
			std::vector<Bond> m_PreviousBonds;
			std::vector<std::size_t> m_DeletedIndices;
		};

		class DuplicateSelectedAtomsCommand final : public ICommand
		{
		public:
			DuplicateSelectedAtomsCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				ElementPropertiesTable elementPropertiesTable,
				std::string windowId)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
				  m_WindowId(std::move(windowId))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"DuplicateSelectedAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				if (windowState.selectedAtomIndices.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.nothing_selected",
						"No atoms selected.",
						"DuplicateSelectedAtomsCommand: selectedAtomIndices is empty.");
				}

				m_WindowIdResolved = windowState.windowId;
				m_PreviousAtoms = target->record->structure.atoms;
				m_PreviousBonds = target->record->structure.bonds;
				m_SourceIndices = windowState.selectedAtomIndices;
				std::sort(m_SourceIndices.begin(), m_SourceIndices.end());

				// Blender's duplicate drops the copy right next to the original rather than
				// exactly on top of it - small fixed offset, no smarter placement heuristic.
				constexpr glm::vec3 kDuplicateOffset(0.5f, 0.0f, 0.0f);
				CrystalStructure &structure = target->record->structure;
				std::vector<std::size_t> newIndices;
				newIndices.reserve(m_SourceIndices.size());
				for (std::size_t sourceIndex : m_SourceIndices)
				{
					if (sourceIndex >= structure.atoms.size())
						continue;

					PointDefectOperation operation;
					operation.atom = structure.atoms[sourceIndex];
					operation.atom.position += kDuplicateOffset;
					operation.atom.fractional = structure.CartesianToFractional(operation.atom.position);
					newIndices.push_back(structure.atoms.size());
					Result<void> result = ApplyInterstitial(structure, operation);
					if (!result)
						return result.Error();
				}

				RegenerateAutoBonds(structure, m_ElementPropertiesTable);
				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, newIndices);
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"DuplicateSelectedAtomsCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.atoms = m_PreviousAtoms;
				target->record->structure.bonds = m_PreviousBonds;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, m_SourceIndices);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Duplicate selected atoms";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_AtomStyleTable;
			ElementPropertiesTable m_ElementPropertiesTable;
			std::string m_WindowId;
			std::string m_WindowIdResolved;
			std::vector<AtomSite> m_PreviousAtoms;
			std::vector<Bond> m_PreviousBonds;
			std::vector<std::size_t> m_SourceIndices;
		};
		class TransformSelectedAtomsCommand final : public ICommand
		{
		public:
			TransformSelectedAtomsCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				GizmoTransformPayload payload)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_Payload(std::move(payload))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"TransformSelectedAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_Payload.windowId);
				if (!target)
					return target.Error();

				m_WindowIdResolved = target->windowState->windowId;
				CrystalStructure &structure = target->record->structure;
				m_PreviousAtoms = structure.atoms;

				for (std::size_t i = 0; i < m_Payload.atomIndices.size(); ++i)
				{
					const std::size_t atomIndex = m_Payload.atomIndices[i];
					if (atomIndex >= structure.atoms.size())
						continue;
					structure.atoms[atomIndex].position = m_Payload.afterPositions[i];
					structure.atoms[atomIndex].fractional = structure.CartesianToFractional(m_Payload.afterPositions[i]);
				}

				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, m_Payload.atomIndices);
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"TransformSelectedAtomsCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.atoms = m_PreviousAtoms;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, m_Payload.atomIndices);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return m_Payload.description;
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_AtomStyleTable;
			GizmoTransformPayload m_Payload;
			std::string m_WindowIdResolved;
			std::vector<AtomSite> m_PreviousAtoms;
		};
	} // namespace

	Unique<ICommand> CreateDeleteSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		std::string windowId)
	{
		return CreateUnique<DeleteSelectedAtomsCommand>(
			std::move(domainLayer),
			std::move(rendererLayer),
			std::move(atomStyleTable),
			std::move(elementPropertiesTable),
			std::move(windowId));
	}

	Unique<ICommand> CreateDuplicateSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		std::string windowId)
	{
		return CreateUnique<DuplicateSelectedAtomsCommand>(
			std::move(domainLayer),
			std::move(rendererLayer),
			std::move(atomStyleTable),
			std::move(elementPropertiesTable),
			std::move(windowId));
	}

	Unique<ICommand> CreateTransformSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		GizmoTransformPayload payload)
	{
		return CreateUnique<TransformSelectedAtomsCommand>(
			std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(payload));
	}
} // namespace DefectStudio
