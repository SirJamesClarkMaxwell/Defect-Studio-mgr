#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererVacancyCommands.hpp"

#include <optional>
#include <type_traits>
#include <utility>

#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"

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
				target->record->structure.*Field = undo ? m_Previous : *m_Next;
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
			Payload m_Payload;
			std::string m_WindowId;
			std::remove_cvref_t<decltype(std::declval<CrystalStructure &>().*Field)> m_Previous;
			std::optional<std::remove_cvref_t<decltype(std::declval<CrystalStructure &>().*Field)>> m_Next;
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
} // namespace DefectStudio
