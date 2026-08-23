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
	} // namespace

	// Resolves windowId (or the focused viewport if empty) to its live window state and the mutable
	// domain StructureRecord backing it. Shared by every atom-edit command's Execute and Undo (both
	// need to re-resolve rather than cache raw pointers across calls) and by ObjectPropertiesPanel,
	// which reads extra domain-only AtomSite fields (label/charge/occupancy/...) that never made it
	// into the renderer-side flat RendererAtomData.
	Result<AtomEditTarget> ResolveAtomEditTarget(
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

	void RefreshOpenWindowsForElementStyle(RendererLayer &rendererLayer, const std::string &symbol, const AtomRenderStyle &style)
	{
		for (RendererWindowState &windowState : rendererLayer.GetWindows())
		{
			for (RendererAtomData &atom : windowState.structure.atoms)
			{
				if (atom.element == symbol)
				{
					atom.color = style.color;
					atom.radius = style.displayRadius;
				}
			}
			for (RendererBondData &bond : windowState.structure.bonds)
			{
				if (bond.firstAtomIndex >= windowState.structure.atoms.size() ||
					bond.secondAtomIndex >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &atomA = windowState.structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &atomB = windowState.structure.atoms[bond.secondAtomIndex];
				bond.radius = std::max(0.05f, 0.22f * std::min(atomA.radius, atomB.radius));
				bond.gradient.start = atomA.color;
				bond.gradient.finish = atomB.color;
			}
		}
	}

	namespace
	{
		// Rebuilds RendererStructureData from record.structure and re-syncs windowState's ECS
		// scene - the common tail every atom-edit command needs after mutating the domain
		// structure. selectAfter (atom indices in the *new* structure) becomes the selection once
		// synced; empty leaves nothing selected. BuildRendererStructureData always defaults every
		// atom to visible (visibility isn't a domain concept), so hidden atoms are captured from
		// windowState's pre-rebuild state and reapplied by index - exact for commands that don't
		// change atom count/order (transform, undo paths that restore a saved atom array).
		void RebuildAndSync(
			RendererWindowState &windowState,
			const StructureRecord &record,
			const AtomStyleTable &atomStyleTable,
			const std::vector<std::size_t> &selectAfter,
			const std::vector<std::size_t> &selectBondsAfter = {})
		{
			std::vector<std::size_t> hiddenBefore;
			for (std::size_t index = 0; index < windowState.structure.atoms.size(); ++index)
				if (!windowState.structure.atoms[index].visible)
					hiddenBefore.push_back(index);
			// Bond ephemeral hide (VisibilityComponent) is exact only when the rebuild doesn't
			// change bond count/order (mirrors the atom hiddenBefore contract above) - fine for
			// every current caller (transform/type-change keep bond identity; delete/duplicate/
			// paste/connect regenerate bonds and pass an explicit selectBondsAfter instead).
			std::vector<std::size_t> hiddenBondsBefore;
			for (std::size_t index = 0; index < windowState.structure.bonds.size(); ++index)
				if (!windowState.structure.bonds[index].visible)
					hiddenBondsBefore.push_back(index);

			windowState.structure = BuildRendererStructureData(
				record.structure,
				windowState.structure.sourcePath,
				windowState.structure.name,
				atomStyleTable,
				windowState.structure.domainStructureId);
			SceneSystem::SyncSceneWithStructure(windowState.sceneRegistry, windowState.structure);
			if (!selectAfter.empty() || !hiddenBefore.empty() || !selectBondsAfter.empty() || !hiddenBondsBefore.empty())
				SceneSystem::ApplySelectionAndVisibilityToScene(
					windowState.sceneRegistry, selectAfter, hiddenBefore, selectBondsAfter, hiddenBondsBefore);
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}

		// In-process atom clipboard for Copy/Paste (Ctrl+C/Ctrl+V) - shared across every window and
		// every command instance, matching how a real OS clipboard behaves (copy in one viewport,
		// paste into another). No project persistence, no OS clipboard integration - just enough to
		// round-trip within a running session.
		std::vector<AtomSite> &GetAtomClipboard()
		{
			static std::vector<AtomSite> clipboard;
			return clipboard;
		}

		// RendererStructureData::bonds is domain structure.bonds with invisible (hidden Auto) bonds
		// filtered out by BuildRendererBonds, in the same relative order - so the k-th visible domain
		// bond is the k-th entry here. Walk domain bonds counting visible ones to invert that mapping,
		// rather than storing a reverse index anywhere (bond-select/delete are rare enough that an
		// O(bonds) walk per call isn't worth caching).
		[[nodiscard]] std::optional<std::size_t> ResolveDomainBondIndex(
			const CrystalStructure &structure, std::size_t renderBondIndex)
		{
			std::size_t visibleCount = 0;
			for (std::size_t domainIndex = 0; domainIndex < structure.bonds.size(); ++domainIndex)
			{
				if (!structure.bonds[domainIndex].visible)
					continue;
				if (visibleCount == renderBondIndex)
					return domainIndex;
				++visibleCount;
			}
			return std::nullopt;
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
				if (windowState.selectedAtomIndices.empty() && windowState.selectedBondIndices.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.nothing_selected",
						"No atoms or bonds selected.",
						"DeleteSelectedAtomsCommand: selectedAtomIndices and selectedBondIndices are both empty.");
				}

				m_WindowIdResolved = windowState.windowId;
				m_PreviousAtoms = target->record->structure.atoms;
				m_PreviousBonds = target->record->structure.bonds;
				m_DeletedIndices = windowState.selectedAtomIndices;
				std::sort(m_DeletedIndices.begin(), m_DeletedIndices.end());

				// Resolve+mutate selected bonds first, while atom indices (and therefore bond
				// endpoint indices) are still the ones selectedBondIndices was resolved against -
				// ApplyVacancy below reindexes/strips bonds as a side effect of deleting atoms, so
				// doing this after would risk acting on stale bond identities. Manual bonds are a
				// real user-authored edge (J) so Delete removes them outright; Auto bonds are
				// data-derived and would just regenerate on the next RegenerateAutoBonds call, so
				// Delete only hides them (Bond::visible = false, preserved across regen - see
				// BondGenerator::RegenerateAutoBonds).
				std::vector<std::size_t> deletedDomainBondIndices;
				for (const std::size_t renderBondIndex : windowState.selectedBondIndices)
				{
					const std::optional<std::size_t> domainIndex =
						ResolveDomainBondIndex(target->record->structure, renderBondIndex);
					if (domainIndex.has_value())
						deletedDomainBondIndices.push_back(*domainIndex);
				}
				std::sort(deletedDomainBondIndices.begin(), deletedDomainBondIndices.end());
				deletedDomainBondIndices.erase(
					std::unique(deletedDomainBondIndices.begin(), deletedDomainBondIndices.end()),
					deletedDomainBondIndices.end());
				for (auto it = deletedDomainBondIndices.rbegin(); it != deletedDomainBondIndices.rend(); ++it)
				{
					Bond &bond = target->record->structure.bonds[*it];
					if (bond.origin == BondOrigin::Manual)
						target->record->structure.bonds.erase(target->record->structure.bonds.begin() + *it);
					else
						bond.visible = false;
				}

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
				return "Delete selection";
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
		class NudgeSelectedAtomsCommand final : public ICommand
		{
		public:
			NudgeSelectedAtomsCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				glm::vec2 screenDirection)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_ScreenDirection(screenDirection)
			{
			}

			Result<void> Execute(CommandContext &context) override
			{
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (domainLayer == nullptr || rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers",
						"Renderer/Domain layer unavailable.",
						"NudgeSelectedAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, "");
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				if (windowState.selectedAtomIndices.empty() || windowState.camera == nullptr)
					return {}; // Nothing to nudge - a quiet no-op, not a user-facing error.

				// Screen-space (camera-right/camera-up), not world axes - Shift+Left always nudges
				// "left as you're looking at it" regardless of how the view is currently orbited,
				// matching the pinned-measurement drag and the rotate trackball elsewhere in this file.
				constexpr float kNudgeStepWorldUnits = 0.15f;
				const glm::mat4 view = windowState.camera->ViewMatrix();
				const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
				const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
				const glm::vec3 worldDelta =
					(cameraRight * m_ScreenDirection.x + cameraUp * m_ScreenDirection.y) * kNudgeStepWorldUnits;

				GizmoTransformPayload payload;
				payload.windowId = windowState.windowId;
				payload.atomIndices = windowState.selectedAtomIndices;
				payload.afterPositions.reserve(payload.atomIndices.size());
				for (const std::size_t atomIndex : payload.atomIndices)
				{
					const glm::vec3 before = atomIndex < windowState.structure.atoms.size()
						? windowState.structure.atoms[atomIndex].cartesianPosition
						: glm::vec3(0.0f);
					payload.afterPositions.push_back(before + worldDelta);
				}
				payload.description = "Move selected atoms";

				m_Inner = CreateTransformSelectedAtomsCommand(m_DomainLayer, m_RendererLayer, m_AtomStyleTable, std::move(payload));
				return m_Inner->Execute(context);
			}

			Result<void> Undo(CommandContext &context) override
			{
				return m_Inner != nullptr ? m_Inner->Undo(context) : Result<void>{};
			}

			[[nodiscard]] std::string Description() const override
			{
				return m_Inner != nullptr ? m_Inner->Description() : "Move selected atoms";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_AtomStyleTable;
			glm::vec2 m_ScreenDirection;
			Unique<ICommand> m_Inner;
		};

		class CopySelectedAtomsCommand final : public ICommand
		{
		public:
			CopySelectedAtomsCommand(
				WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer, std::string windowId)
				: m_DomainLayer(std::move(domainLayer)), m_RendererLayer(std::move(rendererLayer)), m_WindowId(std::move(windowId))
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
						"CopySelectedAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowId);
				if (!target)
					return target.Error();

				const RendererWindowState &windowState = *target->windowState;
				if (windowState.selectedAtomIndices.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.nothing_selected",
						"No atoms selected.",
						"CopySelectedAtomsCommand: selectedAtomIndices is empty.");
				}

				std::vector<std::size_t> sourceIndices = windowState.selectedAtomIndices;
				std::sort(sourceIndices.begin(), sourceIndices.end());

				const CrystalStructure &structure = target->record->structure;
				std::vector<AtomSite> &clipboard = GetAtomClipboard();
				clipboard.clear();
				for (std::size_t sourceIndex : sourceIndices)
					if (sourceIndex < structure.atoms.size())
						clipboard.push_back(structure.atoms[sourceIndex]);
				return {};
			}

			Result<void> Undo(CommandContext &) override { return {}; }

			[[nodiscard]] std::string Description() const override
			{
				return "Copy selected atoms";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return false;
			}

		private:
			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			std::string m_WindowId;
		};

		class PasteAtomsCommand final : public ICommand
		{
		public:
			PasteAtomsCommand(
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
						"PasteAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				const std::vector<AtomSite> &clipboard = GetAtomClipboard();
				if (clipboard.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.clipboard_empty",
						"Clipboard is empty - copy atoms first (Ctrl+C).",
						"PasteAtomsCommand: clipboard is empty.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				m_WindowIdResolved = windowState.windowId;
				m_PreviousAtoms = target->record->structure.atoms;
				m_PreviousBonds = target->record->structure.bonds;

				// Same fixed nudge Duplicate uses - avoids dropping the pasted copy exactly on top of
				// the original (same window) with no extra "smart placement" heuristic needed.
				constexpr glm::vec3 kPasteOffset(0.5f, 0.0f, 0.0f);
				CrystalStructure &structure = target->record->structure;
				std::vector<std::size_t> newIndices;
				newIndices.reserve(clipboard.size());
				for (const AtomSite &clipboardAtom : clipboard)
				{
					PointDefectOperation operation;
					operation.atom = clipboardAtom;
					operation.atom.position += kPasteOffset;
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
						"PasteAtomsCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.atoms = m_PreviousAtoms;
				target->record->structure.bonds = m_PreviousBonds;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, {});
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Paste atoms";
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
		};

		class AddAtomAtCoordinatesCommand final : public ICommand
		{
		public:
			AddAtomAtCoordinatesCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				ElementPropertiesTable elementPropertiesTable,
				AddAtomAtCoordinatesPayload payload)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
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
						"AddAtomAtCoordinatesCommand: DomainLayer or RendererLayer expired.");
				}
				if (m_Payload.species.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.empty_species",
						"Enter an element symbol.",
						"AddAtomAtCoordinatesCommand: payload.species is empty.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_Payload.windowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				m_WindowIdResolved = windowState.windowId;
				CrystalStructure &structure = target->record->structure;
				m_PreviousAtoms = structure.atoms;
				m_PreviousBonds = structure.bonds;

				PointDefectOperation operation;
				operation.atom.species = m_Payload.species;
				operation.atom.position =
					m_Payload.isFractional ? structure.FractionalToCartesian(m_Payload.position) : m_Payload.position;
				operation.atom.fractional = structure.CartesianToFractional(operation.atom.position);
				const std::size_t newIndex = structure.atoms.size();
				Result<void> result = ApplyInterstitial(structure, operation);
				if (!result)
					return result.Error();

				RegenerateAutoBonds(structure, m_ElementPropertiesTable);
				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, {newIndex});
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
						"AddAtomAtCoordinatesCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.atoms = m_PreviousAtoms;
				target->record->structure.bonds = m_PreviousBonds;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, {});
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Add atom (" + m_Payload.species + ")";
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
			AddAtomAtCoordinatesPayload m_Payload;
			std::string m_WindowIdResolved;
			std::vector<AtomSite> m_PreviousAtoms;
			std::vector<Bond> m_PreviousBonds;
		};

		class ChangeSelectedAtomTypeCommand final : public ICommand
		{
		public:
			ChangeSelectedAtomTypeCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				ElementPropertiesTable elementPropertiesTable,
				ChangeAtomTypePayload payload)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
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
						"ChangeSelectedAtomTypeCommand: DomainLayer or RendererLayer expired.");
				}
				if (m_Payload.species.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.empty_species",
						"Enter an element symbol.",
						"ChangeSelectedAtomTypeCommand: payload.species is empty.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_Payload.windowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				if (windowState.selectedAtomIndices.empty())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.nothing_selected",
						"No atoms selected.",
						"ChangeSelectedAtomTypeCommand: selectedAtomIndices is empty.");
				}

				m_WindowIdResolved = windowState.windowId;
				m_ChangedIndices = windowState.selectedAtomIndices;
				std::sort(m_ChangedIndices.begin(), m_ChangedIndices.end());
				CrystalStructure &structure = target->record->structure;
				m_PreviousAtoms = structure.atoms;

				for (std::size_t atomIndex : m_ChangedIndices)
				{
					PointDefectOperation operation;
					operation.atomIndex = atomIndex;
					operation.replacementSpecies = m_Payload.species;
					Result<void> result = ApplyReplacement(structure, operation, "Change atom type");
					if (!result)
						return result.Error();
				}

				RegenerateAutoBonds(structure, m_ElementPropertiesTable);
				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, m_ChangedIndices);
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
						"ChangeSelectedAtomTypeCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.atoms = m_PreviousAtoms;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, m_ChangedIndices);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Change atom type to " + m_Payload.species;
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
			ChangeAtomTypePayload m_Payload;
			std::string m_WindowIdResolved;
			std::vector<AtomSite> m_PreviousAtoms;
			std::vector<std::size_t> m_ChangedIndices;
		};

		// Replaces BondGenerationSettings and regenerates Auto bonds from it - also how a plain
		// "rebuild bonds now" works (payload.settings == the structure's current settings, unchanged;
		// RegenerateAutoBonds is deterministic for a fixed atom set + settings). Undo restores the
		// previous settings and regenerates again from THOSE, rather than snapshotting/restoring the
		// raw bond list - equivalent (RegenerateAutoBonds is deterministic and Manual-origin bonds are
		// never touched by it) and correctly reflects "these settings produce these bonds" either way.
		class SetBondSettingsCommand final : public ICommand
		{
		public:
			SetBondSettingsCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				ElementPropertiesTable elementPropertiesTable,
				SetBondSettingsPayload payload)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
				  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
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
						"SetBondSettingsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_Payload.windowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				m_WindowIdResolved = windowState.windowId;
				m_SelectionBefore = windowState.selectedAtomIndices;
				CrystalStructure &structure = target->record->structure;
				m_PreviousSettings = structure.bondSettings;
				structure.bondSettings = m_Payload.settings;

				RegenerateAutoBonds(structure, m_ElementPropertiesTable);
				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, m_SelectionBefore);
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
						"SetBondSettingsCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				CrystalStructure &structure = target->record->structure;
				structure.bondSettings = m_PreviousSettings;
				RegenerateAutoBonds(structure, m_ElementPropertiesTable);
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, m_SelectionBefore);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Change bond generation settings";
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
			SetBondSettingsPayload m_Payload;
			std::string m_WindowIdResolved;
			std::vector<std::size_t> m_SelectionBefore;
			BondGenerationSettings m_PreviousSettings;
		};

		// Manual bond add ('J'). Requires exactly 2 selected atoms - unlike Delete/Duplicate/Paste,
		// does NOT call RegenerateAutoBonds, since that would only rebuild the Auto-origin bonds this
		// command has nothing to do with (see BondOrigin::Manual in CrystalPrimitives.hpp: Manual
		// bonds already survive every Auto regen untouched).
		class ConnectSelectedAtomsCommand final : public ICommand
		{
		public:
			ConnectSelectedAtomsCommand(
				WeakRef<DomainLayer> domainLayer,
				WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable atomStyleTable,
				std::string windowId)
				: m_DomainLayer(std::move(domainLayer)),
				  m_RendererLayer(std::move(rendererLayer)),
				  m_AtomStyleTable(std::move(atomStyleTable)),
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
						"ConnectSelectedAtomsCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowId);
				if (!target)
					return target.Error();

				RendererWindowState &windowState = *target->windowState;
				if (windowState.selectedAtomIndices.size() != 2)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.connect_needs_two",
						"Select exactly 2 atoms to connect.",
						"ConnectSelectedAtomsCommand: selectedAtomIndices.size() == " +
							std::to_string(windowState.selectedAtomIndices.size()) + ", expected 2.");
				}

				CrystalStructure &structure = target->record->structure;
				const std::size_t firstIndex = windowState.selectedAtomIndices[0];
				const std::size_t secondIndex = windowState.selectedAtomIndices[1];
				if (firstIndex >= structure.atoms.size() || secondIndex >= structure.atoms.size())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.connect_bad_index",
						"Selected atom is no longer valid.",
						"ConnectSelectedAtomsCommand: selected atom index out of range.");
				}

				const bool alreadyConnected =
					std::any_of(structure.bonds.begin(), structure.bonds.end(), [&](const Bond &bond) {
						return (bond.firstAtomIndex == firstIndex && bond.secondAtomIndex == secondIndex) ||
							(bond.firstAtomIndex == secondIndex && bond.secondAtomIndex == firstIndex);
					});
				if (alreadyConnected)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.connect_already_bonded",
						"These atoms are already connected.",
						"ConnectSelectedAtomsCommand: a bond between these two atom indices already exists.");
				}

				m_WindowIdResolved = windowState.windowId;
				m_PreviousBonds = structure.bonds;

				Bond bond;
				bond.firstAtomIndex = firstIndex;
				bond.secondAtomIndex = secondIndex;
				bond.lengthAngstrom =
					glm::distance(structure.atoms[firstIndex].position, structure.atoms[secondIndex].position);
				bond.origin = BondOrigin::Manual;
				bond.visible = true;
				structure.bonds.push_back(bond);

				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, windowState.selectedAtomIndices);
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
						"ConnectSelectedAtomsCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				target->record->structure.bonds = m_PreviousBonds;
				RebuildAndSync(*target->windowState, *target->record, m_AtomStyleTable, {});
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Connect selected atoms";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_AtomStyleTable;
			std::string m_WindowId;
			std::string m_WindowIdResolved;
			std::vector<Bond> m_PreviousBonds;
		};

		// Object Properties panel's Label/Charge/Magnetization/Occupancy/selective-dynamics edits -
		// pure AtomSite metadata with no renderer-side representation, so unlike every other atom-edit
		// command here this skips RegenerateAutoBonds and RebuildAndSync entirely: nothing about the
		// window's geometry, style, or ECS scene depends on these fields.
		class SetAtomPropertiesCommand final : public ICommand
		{
		public:
			SetAtomPropertiesCommand(
				WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer, AtomPropertiesPayload payload)
				: m_DomainLayer(std::move(domainLayer)), m_RendererLayer(std::move(rendererLayer)), m_Payload(std::move(payload))
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
						"SetAtomPropertiesCommand: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_Payload.windowId);
				if (!target)
					return target.Error();

				CrystalStructure &structure = target->record->structure;
				if (m_Payload.atomIndex >= structure.atoms.size())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.index_out_of_range",
						"Selected atom is no longer valid.",
						"SetAtomPropertiesCommand: atomIndex out of range.");
				}

				m_WindowIdResolved = target->windowState->windowId;
				m_Previous = structure.atoms[m_Payload.atomIndex];
				applyPayload(structure.atoms[m_Payload.atomIndex]);
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
						"SetAtomPropertiesCommand::Undo: DomainLayer or RendererLayer expired.");
				}

				Result<AtomEditTarget> target = ResolveAtomEditTarget(*rendererLayer, *domainLayer, m_WindowIdResolved);
				if (!target)
					return target.Error();

				CrystalStructure &structure = target->record->structure;
				if (m_Payload.atomIndex >= structure.atoms.size())
				{
					return MakeAtomEditError(
						"renderer.atom_edit.index_out_of_range",
						"Selected atom is no longer valid.",
						"SetAtomPropertiesCommand::Undo: atomIndex out of range.");
				}

				structure.atoms[m_Payload.atomIndex] = m_Previous;
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Set atom properties";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			void applyPayload(AtomSite &atom) const
			{
				atom.label = m_Payload.label;
				atom.charge = m_Payload.charge;
				atom.magnetization = m_Payload.magnetization;
				atom.occupancy = m_Payload.occupancy;
				atom.hasSelectiveDynamics = m_Payload.hasSelectiveDynamics;
				atom.selectiveDynamics = m_Payload.selectiveDynamics;
			}

			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomPropertiesPayload m_Payload;
			std::string m_WindowIdResolved;
			AtomSite m_Previous;
		};

		// Element Catalog's per-element color/radius edit. No DomainLayer involved - style isn't a
		// domain concept - so unlike every other command in this file, Execute/Undo just flip
		// between two AtomStyleTable::SetStyle calls and a window refresh; previousStyle comes from
		// the payload (captured by the panel before its live drag preview started) rather than being
		// self-discovered, since AtomStyleTable is only mutated here, on commit, not during the drag.
		class SetElementStyleCommand final : public ICommand
		{
		public:
			SetElementStyleCommand(WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, SetElementStylePayload payload)
				: m_RendererLayer(std::move(rendererLayer)), m_AtomStyleTable(std::move(atomStyleTable)), m_Payload(std::move(payload))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers", "Renderer layer unavailable.",
						"SetElementStyleCommand: RendererLayer expired.");
				}
				m_AtomStyleTable.SetStyle(m_Payload.symbol, m_Payload.newStyle);
				RefreshOpenWindowsForElementStyle(*rendererLayer, m_Payload.symbol, m_Payload.newStyle);
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
				if (rendererLayer == nullptr)
				{
					return MakeAtomEditError(
						"renderer.atom_edit.no_layers", "Renderer layer unavailable.",
						"SetElementStyleCommand::Undo: RendererLayer expired.");
				}
				m_AtomStyleTable.SetStyle(m_Payload.symbol, m_Payload.previousStyle);
				RefreshOpenWindowsForElementStyle(*rendererLayer, m_Payload.symbol, m_Payload.previousStyle);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Set " + m_Payload.symbol + " style";
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_AtomStyleTable;
			SetElementStylePayload m_Payload;
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

	Unique<ICommand> CreateAddAtomAtCoordinatesCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		AddAtomAtCoordinatesPayload payload)
	{
		return CreateUnique<AddAtomAtCoordinatesCommand>(
			std::move(domainLayer),
			std::move(rendererLayer),
			std::move(atomStyleTable),
			std::move(elementPropertiesTable),
			std::move(payload));
	}

	Unique<ICommand> CreateConnectSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		std::string windowId)
	{
		return CreateUnique<ConnectSelectedAtomsCommand>(
			std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), std::move(windowId));
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

	Unique<ICommand> CreateNudgeSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		glm::vec2 screenDirection)
	{
		return CreateUnique<NudgeSelectedAtomsCommand>(
			std::move(domainLayer), std::move(rendererLayer), std::move(atomStyleTable), screenDirection);
	}

	Unique<ICommand> CreateCopySelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer, AtomStyleTable, std::string windowId)
	{
		return CreateUnique<CopySelectedAtomsCommand>(std::move(domainLayer), std::move(rendererLayer), std::move(windowId));
	}

	Unique<ICommand> CreatePasteAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		std::string windowId)
	{
		return CreateUnique<PasteAtomsCommand>(
			std::move(domainLayer),
			std::move(rendererLayer),
			std::move(atomStyleTable),
			std::move(elementPropertiesTable),
			std::move(windowId));
	}

	Unique<ICommand> CreateChangeSelectedAtomTypeCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		ChangeAtomTypePayload payload)
	{
		return CreateUnique<ChangeSelectedAtomTypeCommand>(
			std::move(domainLayer),
			std::move(rendererLayer),
			std::move(atomStyleTable),
			std::move(elementPropertiesTable),
			std::move(payload));
	}

	Unique<ICommand> CreateSetAtomPropertiesCommand(
		WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer, AtomPropertiesPayload payload)
	{
		return CreateUnique<SetAtomPropertiesCommand>(std::move(domainLayer), std::move(rendererLayer), std::move(payload));
	}

	Unique<ICommand> CreateSetElementStyleCommand(
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, SetElementStylePayload payload)
	{
		return CreateUnique<SetElementStyleCommand>(std::move(rendererLayer), std::move(atomStyleTable), std::move(payload));
	}

	Unique<ICommand> CreateSetBondSettingsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		SetBondSettingsPayload payload)
	{
		return CreateUnique<SetBondSettingsCommand>(
			std::move(domainLayer),
			std::move(rendererLayer),
			std::move(atomStyleTable),
			std::move(elementPropertiesTable),
			std::move(payload));
	}
} // namespace DefectStudio
