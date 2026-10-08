#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererVacancyCommands.hpp"

#include <algorithm>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneReferenceRemap.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		// Replaces one field of the domain structure. Field = CrystalStructure::vacancies or
		// CrystalStructure::defectFrame; Payload carries windowId, description and either the new
		// value or an `edit` applied to the domain's current one (resolved once, on the first run,
		// so redo replays the same result).
		template <auto Field, typename Payload, auto Value>
		class SetStructureFieldCommand final : public ICommand
		{
		public:
			SetStructureFieldCommand(WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable styles, Payload payload)
				: m_DomainLayer(std::move(domainLayer)), m_RendererLayer(std::move(rendererLayer)),
				  m_Styles(std::move(styles)), m_Payload(std::move(payload)) {}

			Result<void> Execute(CommandContext &) override { return apply(false); }
			Result<void> Undo(CommandContext &) override { return apply(true); }
			std::string Description() const override { return m_Payload.description; }
			bool IsUndoable() const noexcept override { return true; }

		private:
			Result<void> apply(bool undo)
			{
				const auto domain = m_DomainLayer.lock();
				const auto renderer = m_RendererLayer.lock();
				if (domain == nullptr || renderer == nullptr)
					return StructuredError{ErrorCategory::Validation, Severity::Error,
						"Renderer/Domain layer unavailable.", "SetStructureFieldCommand: layer expired.",
						"Open a viewport with an editable structure.", "RendererVacancyCommands",
						"renderer.atom_edit.no_layers"};
				auto target = ResolveAtomEditTarget(*renderer, *domain, undo ? m_WindowId : m_Payload.windowId);
				if (!target)
					return target.Error();
				if (!undo)
				{
					m_WindowId = target->windowState->windowId;
					m_Previous = target->record->structure.*Field;
					if (!m_Next)
					{
						m_Next.emplace(m_Payload.edit ? m_Previous : m_Payload.*Value);
						if (m_Payload.edit)
							m_Payload.edit(*m_Next, target->record->structure);
					}
				}
				std::vector<std::size_t> removed;
				if constexpr (std::is_same_v<Payload, SetVacanciesPayload>)
					if (m_Next->size() < m_Previous.size())
					{
						// Removal keeps list order. Match every field so coincident, differently styled sites stay distinct.
						std::size_t next = 0;
						for (std::size_t old = 0; old < m_Previous.size(); ++old)
						{
							const auto &a = m_Previous[old];
							if (next < m_Next->size())
							{
								const auto &b = (*m_Next)[next];
								if (a.position == b.position && a.fractional == b.fractional &&
									a.sourceSpecies == b.sourceSpecies && a.label == b.label &&
									a.index == b.index && a.color == b.color && a.hidden == b.hidden)
								{ ++next; continue; }
							}
							removed.push_back(old);
						}
						if (!undo)
							for (auto &window : renderer->GetWindows())
								if (window.structure.domainStructureId == target->windowState->structure.domainStructureId)
								{
									m_SceneBefore.insert_or_assign(window.windowId, WindowSnapshot{
										CaptureSceneObjectsSnapshot(window), window.defectFrameChildren});
									if (!m_SceneAfter.contains(window.windowId))
										RemapSceneVacancyReferencesAfterVacancyRemoval(window, removed);
								}
					}
				target->record->structure.*Field = undo ? m_Previous : *m_Next;
				domain->Workspace().Structures().MarkModified(target->record->id);
				for (RendererWindowState &window : renderer->GetWindows())
					if (window.structure.domainStructureId == target->windowState->structure.domainStructureId)
					{
						const bool hadFrame = window.structure.defectFrame.has_value();
						RebuildAndSync(window, *target->record, m_Styles,
							window.selectedAtomIndices, window.selectedBondIndices);
						if (!removed.empty())
						{
							const auto &snapshots = undo ? m_SceneBefore : m_SceneAfter;
							if (const auto snapshot = snapshots.find(window.windowId); snapshot != snapshots.end())
							{
								RestoreSceneObjectsSnapshot(window, snapshot->second.scene);
								window.defectFrameChildren = snapshot->second.children;
							}
							else if (!undo)
							{
								SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
								m_SceneAfter.emplace(window.windowId, WindowSnapshot{
									CaptureSceneObjectsSnapshot(window), window.defectFrameChildren});
							}
						}
						if constexpr (std::is_same_v<Payload, SetDefectFramePayload>)
						{
							if (!hadFrame && window.structure.defectFrame)
								window.transformOrientation = TransformOrientation::Defect;
							else if (!window.structure.defectFrame)
							{
								window.transformOrientation = TransformOrientation::Global;
								window.defectFrameSelected = false;
								window.defectFrameChildren = {};
							}
						}
						std::erase_if(window.selectedVacancies, [&](std::size_t index) {
							return index >= window.structure.vacancies.size() || window.structure.vacancies[index].hidden;
						});
					}
				return {};
			}

			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_Styles;
			Payload m_Payload;
			std::string m_WindowId;
			std::remove_cvref_t<decltype(std::declval<CrystalStructure &>().*Field)> m_Previous;
			std::optional<std::remove_cvref_t<decltype(std::declval<CrystalStructure &>().*Field)>> m_Next;
			struct WindowSnapshot
			{
				SceneObjectsSnapshot scene;
				RendererWindowState::DefectFrameChildren children;
			};
			std::unordered_map<std::string, WindowSnapshot> m_SceneBefore, m_SceneAfter;
		};
	} // namespace

	Unique<ICommand> CreateSetVacanciesCommand(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, SetVacanciesPayload payload)
	{
		return CreateUnique<SetStructureFieldCommand<&CrystalStructure::vacancies, SetVacanciesPayload,
			&SetVacanciesPayload::vacancies>>(std::move(domainLayer), std::move(rendererLayer),
			std::move(atomStyleTable), std::move(payload));
	}

	Unique<ICommand> CreateSetDefectFrameCommand(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, SetDefectFramePayload payload)
	{
		return CreateUnique<SetStructureFieldCommand<&CrystalStructure::defectFrame, SetDefectFramePayload,
			&SetDefectFramePayload::frame>>(std::move(domainLayer), std::move(rendererLayer),
			std::move(atomStyleTable), std::move(payload));
	}
	void BindRendererVacancyVisibilityEditor(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable)
	{
		if (const auto renderer = rendererLayer.lock())
			renderer->BindVacancyVisibilityEditor([domainLayer, rendererLayer, atomStyleTable](RendererWindowState &window, bool showAll) -> Result<void> {
				const auto selected = window.selectedVacancies;
				SetVacanciesPayload payload{window.windowId, {}, showAll ? "Show vacancies" : "Hide vacancies"};
				payload.edit = [selected, showAll](std::vector<VacancySite> &vacancies, const CrystalStructure &) {
					for (std::size_t index = 0; index < vacancies.size(); ++index)
						if (showAll || std::find(selected.begin(), selected.end(), index) != selected.end())
							vacancies[index].hidden = !showAll;
				};
				// H itself runs inside CommandRegistry::Execute; execute the existing child command
				// directly to avoid a rejected nested registry call, and join the visibility UndoScope.
				auto command = CreateSetVacanciesCommand(domainLayer, rendererLayer, atomStyleTable, std::move(payload));
				CommandContext context;
				const auto result = command->Execute(context);
				if (!result) return result;
				if (const auto layer = rendererLayer.lock())
					if (const auto stack = layer->GetUndoStackHandle().lock())
						(void)stack->PushExecuted(std::move(command));
				return {};
			});
	}
} // namespace DefectStudio
