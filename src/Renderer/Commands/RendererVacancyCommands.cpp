#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererVacancyCommands.hpp"

#include <utility>

#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		class SetVacanciesCommand final : public ICommand
		{
		public:
			SetVacanciesCommand(WeakRef<DomainLayer> domainLayer, WeakRef<RendererLayer> rendererLayer,
				AtomStyleTable styles, SetVacanciesPayload payload)
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
						"Renderer/Domain layer unavailable.", "SetVacanciesCommand: layer expired.",
						"Open a viewport with an editable structure.", "RendererVacancyCommands",
						"renderer.atom_edit.no_layers"};
				auto target = ResolveAtomEditTarget(*renderer, *domain, undo ? m_WindowId : m_Payload.windowId);
				if (!target)
					return target.Error();
				if (!undo)
				{
					m_WindowId = target->windowState->windowId;
					m_Previous = target->record->structure.vacancies;
				}
				target->record->structure.vacancies = undo ? m_Previous : m_Payload.vacancies;
				domain->Workspace().Structures().MarkModified(target->record->id);
				for (RendererWindowState &window : renderer->GetWindows())
					if (window.structure.domainStructureId == target->windowState->structure.domainStructureId)
						RebuildAndSync(window, *target->record, m_Styles,
							window.selectedAtomIndices, window.selectedBondIndices);
				return {};
			}

			WeakRef<DomainLayer> m_DomainLayer;
			WeakRef<RendererLayer> m_RendererLayer;
			AtomStyleTable m_Styles;
			SetVacanciesPayload m_Payload;
			std::string m_WindowId;
			std::vector<VacancySite> m_Previous;
		};
	} // namespace

	Unique<ICommand> CreateSetVacanciesCommand(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable, SetVacanciesPayload payload)
	{
		return CreateUnique<SetVacanciesCommand>(std::move(domainLayer), std::move(rendererLayer),
			std::move(atomStyleTable), std::move(payload));
	}
} // namespace DefectStudio
