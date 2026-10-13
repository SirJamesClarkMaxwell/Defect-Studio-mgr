#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererCellEditCommand.hpp"

#include <numeric>
#include <unordered_map>

#include "Domain/Crystal/BondGenerator.hpp"
#include "Domain/Crystal/CellEditing.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/HiddenSceneState.hpp"
#include "Renderer/Scene/SceneReferenceRemap.hpp"
#include "Renderer/Scene/SceneSelection.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		class ReplaceStructureCommand final : public ICommand
		{
		public:
			ReplaceStructureCommand(WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable styles, ElementPropertiesTable elements, ReplaceStructurePayload payload)
				: m_DomainLayer(std::move(domainLayer)), m_RendererLayer(std::move(rendererLayer)),
				  m_Styles(std::move(styles)), m_Elements(std::move(elements)), m_Payload(std::move(payload))
			{
			}

			Result<void> Execute(CommandContext &) override { return apply(false); }
			Result<void> Undo(CommandContext &) override { return apply(true); }
			std::string Description() const override { return m_Payload.description; }
			bool IsUndoable() const noexcept override { return true; }

		private:
			struct WindowState
			{
				HiddenSceneState hidden;
				SceneObjectsSnapshot scene;
				RendererWindowState::DefectFrameChildren children;
				TransformOrientation orientation;
			};

			Result<void> apply(bool undo)
			{
				const auto domain = m_DomainLayer.lock();
				const auto renderer = m_RendererLayer.lock();
				if (!domain || !renderer)
					return error("Renderer/Domain layer unavailable.");
				auto target = ResolveAtomEditTarget(*renderer, *domain, m_Payload.windowId);
				if (!target)
					return target.Error();
				const auto structureId = target->windowState->structure.domainStructureId;
				if (m_Previous && structureId != m_StructureId)
					return error("The window now contains a different structure.");
				if (undo && !m_Previous)
					return error("The command has not been executed.");

				const bool firstRun = !m_Previous;
				if (firstRun)
				{
					if (!target->record->structure.isPeriodic || !m_Payload.structure.isPeriodic)
						return error("Cell editing requires a periodic structure.");
					const auto validated = WithCell(m_Payload.structure, m_Payload.structure.cell, CellEditPositions::KeepCartesian);
					if (!validated)
						return validated.Error();
					RegenerateAutoBonds(m_Payload.structure, m_Elements);
					m_Previous = target->record->structure;
					m_StructureId = structureId;
					m_Payload.windowId = target->windowState->windowId;
				}

				for (auto &window : renderer->GetWindows())
				{
					if (window.structure.domainStructureId != structureId)
						continue;
					if (firstRun)
					{
						m_Before.emplace(window.windowId, WindowState{CaptureHiddenSceneState(window),
							CaptureSceneObjectsSnapshot(window), window.defectFrameChildren, window.transformOrientation});
						if (m_Payload.changesSiteIndices)
						{
							// BuildSupercell drops vacancies; detach their annotations before rebuilding.
							std::vector<std::size_t> vacancies(window.structure.vacancies.size());
							std::iota(vacancies.begin(), vacancies.end(), 0);
							RemapSceneVacancyReferencesAfterVacancyRemoval(window, vacancies);
						}
					}
				}

				target->record->structure = undo ? *m_Previous : m_Payload.structure;
				domain->Workspace().Structures().MarkStructureFileModified(target->record->id);
				for (auto &window : renderer->GetWindows())
				{
					if (window.structure.domainStructureId != structureId)
						continue;
					const auto &snapshots = undo ? m_Before : m_After;
					const auto snapshot = snapshots.find(window.windowId);
					const HiddenSceneState emptyHidden;
					const auto &hidden = snapshot != snapshots.end() ? snapshot->second.hidden :
						(m_Payload.changesSiteIndices ? emptyHidden : CaptureHiddenSceneState(window));
					RebuildAndSync(window, *target->record, m_Styles, {}, {}, &hidden);
					if (snapshot != snapshots.end())
					{
						RestoreSceneObjectsSnapshot(window, snapshot->second.scene);
						window.defectFrameChildren = snapshot->second.children;
						window.transformOrientation = snapshot->second.orientation;
					}
					if (!window.structure.defectFrame)
					{
						window.defectFrameChildren = {};
						if (window.transformOrientation == TransformOrientation::Defect)
							window.transformOrientation = TransformOrientation::Global;
					}
					ClearAllSceneSelection(window);
					SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
					if (firstRun)
						m_After.emplace(window.windowId, WindowState{CaptureHiddenSceneState(window),
							CaptureSceneObjectsSnapshot(window), window.defectFrameChildren, window.transformOrientation});
				}
				return {};
			}

			static StructuredError error(std::string details)
			{
				return StructuredError{ErrorCategory::Validation, Severity::Error,
					"Nie można zmienić komórki.", std::move(details), "Otwórz edytowalną strukturę okresową.",
					"RendererCellEditCommand", "renderer.cell_edit.invalid_target"};
			}

			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_Styles;
			ElementPropertiesTable m_Elements;
			ReplaceStructurePayload m_Payload;
			std::optional<CrystalStructure> m_Previous;
			std::string m_StructureId;
			std::unordered_map<std::string, WindowState> m_Before, m_After;
		};
	} // namespace

	Unique<ICommand> CreateReplaceStructureCommand(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable styles,
		ElementPropertiesTable elements, ReplaceStructurePayload payload)
	{
		return CreateUnique<ReplaceStructureCommand>(std::move(domainLayer), std::move(rendererLayer),
			std::move(styles), std::move(elements), std::move(payload));
	}
} // namespace DefectStudio
