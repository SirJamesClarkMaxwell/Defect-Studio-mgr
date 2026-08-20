#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;

	// T08.6.3 controls: calc-directory loading, band range, bulk (VBM/CBM) reference, band table,
	// wavefunction render controls. The occupation diagram itself lives in OccupationDiagramPanel
	// (its own window, so it can fill the whole thing) - both share one ElectronicStructureSession.
	class ElectronicStructurePanel final : public IPanel
	{
	public:
		explicit ElectronicStructurePanel(
			Ref<ElectronicStructureSession> session,
			Ref<EventBus> eventBus,
			std::string title = "Electronic Structure",
			bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void renderBandTable(
			ElectronicStructureSession::WindowState &state, const std::vector<OrbitalRecord> &filtered,
			RendererWindowState &windowState, float vbm);
		void renderWavefunctionControls(ElectronicStructureSession::WindowState &state, RendererWindowState &windowState);
		void renderBulkReferenceControls();

		Ref<ElectronicStructureSession> m_Session;
		Ref<EventBus> m_EventBus;
	};
} // namespace DefectStudio
