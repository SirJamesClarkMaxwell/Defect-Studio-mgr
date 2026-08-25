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
		// Small editable (irrep -> custom label) table, persisted per-project - see
		// ProjectEvents::IrrepLabelOverridesChanged and ProjectManifest::irrepLabelOverrides.
		void renderIrrepLabelEditor();

		Ref<ElectronicStructureSession> m_Session;
		Ref<EventBus> m_EventBus;
		char m_NewIrrepKey[32]{};
		char m_NewIrrepLabel[128]{};
		// Empty = use ExportOccupationDiagramImage's own default
		// (<calculationDirectory>/occupation_diagram.png) - set via the "Browse..." button.
		Path m_ImageExportPath;
		bool m_ImageExportLightBackground = false;
	};
} // namespace DefectStudio
