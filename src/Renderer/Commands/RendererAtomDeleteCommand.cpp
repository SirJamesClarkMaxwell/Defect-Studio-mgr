#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererAtomEditCommands.hpp"

#include <algorithm>
#include <utility>

#include "Domain/Defects/DefectModel.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/HiddenSceneState.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneReferenceRemap.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

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
				m_PreviousVacancies = target->record->structure.vacancies;
				m_HiddenBefore = CaptureHiddenSceneState(windowState);
				m_DeletedIndices = windowState.selectedAtomIndices;
				std::sort(m_DeletedIndices.begin(), m_DeletedIndices.end());
				m_DeletedIndices.erase(std::unique(m_DeletedIndices.begin(), m_DeletedIndices.end()), m_DeletedIndices.end());
				if (!m_DeletedIndices.empty() && m_DeletedIndices.back() >= m_PreviousAtoms.size())
					return MakeAtomEditError("renderer.atom_edit.index_out_of_range",
						"Wybrany atom nie jest już dostępny.", "Selected atom index is out of range.");
				m_SceneBefore = CaptureSceneObjectsSnapshot(windowState);
				m_DefectChildrenBefore = windowState.defectFrameChildren;
				m_DisplacementBefore = windowState.displacementComparison;

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

				const HiddenSceneState hiddenAfterDelete =
					RemapHiddenSceneStateAfterAtomRemoval(m_HiddenBefore, m_DeletedIndices);
				std::vector<std::size_t> vacancyIndices;
				for (std::size_t index = 0; index < m_DeletedIndices.size(); ++index)
					vacancyIndices.push_back(m_PreviousVacancies.size() + m_DeletedIndices.size() - 1 - index);
				RemapSceneAtomReferencesAfterAtomRemoval(windowState, m_DeletedIndices, vacancyIndices);
				if (!m_DeletedIndices.empty())
					domainLayer->Workspace().Structures().MarkStructureFileModified(target->record->id);
				else
					domainLayer->Workspace().Structures().MarkModified(target->record->id);
				RebuildAndSync(windowState, *target->record, m_AtomStyleTable, {}, {}, &hiddenAfterDelete);
				SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
				std::vector<std::size_t> newVacancies;
				for (std::size_t index = m_PreviousVacancies.size(); index < windowState.structure.vacancies.size(); ++index)
					newVacancies.push_back(index);
				if (m_SceneAfter)
					RestoreSceneObjectsSnapshot(windowState, *m_SceneAfter);
				else
				{
					if (!newVacancies.empty()) (void)AddVacancyLabels(windowState, newVacancies);
					m_SceneAfter = CaptureSceneObjectsSnapshot(windowState);
				}
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
				target->record->structure.vacancies = m_PreviousVacancies;
				auto &window = *target->windowState;
				window.defectFrameChildren = m_DefectChildrenBefore;
				window.displacementComparison = m_DisplacementBefore;
				RestoreSceneObjectsSnapshot(window, m_SceneBefore);
				if (!m_DeletedIndices.empty())
					domainLayer->Workspace().Structures().MarkStructureFileModified(target->record->id);
				else
					domainLayer->Workspace().Structures().MarkModified(target->record->id);
				RebuildAndSync(
					*target->windowState, *target->record, m_AtomStyleTable, m_DeletedIndices, {}, &m_HiddenBefore);
				SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
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
			std::vector<VacancySite> m_PreviousVacancies;
			std::vector<std::size_t> m_DeletedIndices;
			HiddenSceneState m_HiddenBefore;
			SceneObjectsSnapshot m_SceneBefore;
			std::optional<SceneObjectsSnapshot> m_SceneAfter;
			RendererWindowState::DefectFrameChildren m_DefectChildrenBefore;
			std::optional<RendererWindowState::DisplacementComparisonState> m_DisplacementBefore;
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

} // namespace DefectStudio
