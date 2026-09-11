// The CreationSession side of the wizard: creating the session as soon as the draft is valid,
// keeping the registry copy current, and reading gizmo drags back into the basis table.
// Split out of NewStructureWizardPanel.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/epsilon.hpp>

#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Domain/Crystal/PrimitiveCell.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	Ref<CreationSession> NewStructureWizardPanel::activeSession() const
	{
		if (m_SessionRegistry == nullptr || !m_SessionId.has_value())
			return nullptr;
		return m_SessionRegistry->Find(*m_SessionId).lock();
	}

	bool NewStructureWizardPanel::pullGizmoEditsFromPreview()
	{
		const Ref<CreationSession> session = activeSession();
		if (session == nullptr || session->previewWindowIds.empty())
			return false;

		// Pane 0 is the BASIS pane - the only one showing exactly the motif rows, one atom per row.
		// The unit-cell and supercell panes are expansions, so a drag there has no single row to
		// write back to.
		const std::string &unitCellWindowId = session->previewWindowIds.front();
		if (unitCellWindowId.empty())
			return false;

		std::vector<RendererWindowState> &windows = m_RendererLayer.GetWindows();
		const auto it = std::find_if(windows.begin(), windows.end(), [&](const RendererWindowState &window) {
			return window.windowId == unitCellWindowId;
		});
		if (it == windows.end())
			return false;

		// Read the pane back into the table ONLY while the gizmo owns the atoms, plus the one frame
		// after it lets go (the drag clears gizmoDragActive before the commit block runs). Doing it
		// unconditionally is what killed typing into the basis table: the pane still held the old
		// position, so every edit was overwritten in the same frame it was made, the draft never
		// changed, and the previews had no reason to redraw.
		const bool dragging = it->gizmoDragActive;
		const bool readBack = dragging || m_GizmoDragWasActive;
		m_GizmoDragWasActive = dragging;
		if (!readBack)
			return false;

		// The preview is built straight from m_BasisRows in order, and neither RegenerateAutoBonds
		// nor BuildRendererStructureData reorders atoms, so index i is row i. A mismatch means the
		// window is showing something else entirely - bail rather than write coordinates into the
		// wrong rows.
		if (it->structure.atoms.size() != m_BasisRows.size())
			return false;

		const glm::mat3 lattice = BuildLatticeCell(m_System, m_Params).ToMatrix();
		if (std::abs(glm::determinant(lattice)) < 1e-6f)
			return false;
		const glm::mat3 inverseLattice = glm::inverse(lattice);

		for (std::size_t i = 0; i < m_BasisRows.size(); ++i)
		{
			const glm::vec3 fractional = inverseLattice * it->structure.atoms[i].cartesianPosition;
			if (glm::all(glm::epsilonEqual(fractional, m_BasisRows[i].fractional, 1e-5f)))
				continue;
			m_BasisRows[i].fractional = fractional;
		}

		return dragging;
	}

	void NewStructureWizardPanel::syncDraftToSession()
	{
		const Ref<CreationSession> session = activeSession();
		if (session == nullptr)
			return;

		const CrystalStructure unitCell = buildStructure();

		if (m_ShowPrimitiveCell && m_Centering != BravaisCenteringPreset::Primitive)
			session->primitiveCellOverlay = PrimitiveCellVectors(unitCell.cell.ToMatrix(), m_Centering);
		else
			session->primitiveCellOverlay.reset();

		session->draftStructure = unitCell;
		session->motifStructure = buildMotifStructure();
		session->displayName = m_StructureNameBuffer.data();
		session->supercellCounts = m_SupercellCounts;
		session->exportPotcar = m_ExportPotcar;
		session->previewVisible = m_ViewVisible;
		session->dirty = true;
		session->lastModifiedAt = Time::Now();
	}

	void NewStructureWizardPanel::ensureSession()
	{
		if (m_SessionRegistry == nullptr || m_EventBus == nullptr || activeSession() != nullptr)
			return;

		Ref<CreationSession> session = m_SessionRegistry->Create(m_Mode);
		if (session == nullptr)
			return;
		m_SessionId = session->sessionId;

		DomainEvents::CreationSessionCreated createdEvent;
		createdEvent.sessionId = session->sessionId;
		m_EventBus->Publish(createdEvent);
	}

	void NewStructureWizardPanel::moveToStructureHub()
	{
		ensureSession();
		const Ref<CreationSession> session = activeSession();
		if (session == nullptr || m_EventBus == nullptr)
			return;

		session->mode = m_Mode;
		syncDraftToSession();

		// targetDirectory is left empty on purpose: this panel does not know the Project Tree
		// selection. The Structure Hub captures it, and falls back to the live selection when the
		// session carries none.
		DomainEvents::SessionReadyForStructureHub readyEvent;
		readyEvent.sessionId = session->sessionId;
		m_EventBus->Publish(readyEvent);
	}
} // namespace DefectStudio
