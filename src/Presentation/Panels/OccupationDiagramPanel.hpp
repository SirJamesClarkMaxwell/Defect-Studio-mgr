#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	// The occupation-diagram half of T08.6.3, split out from ElectronicStructurePanel so it can be
	// its own dockable window and the plot can fill the whole thing instead of a fixed height
	// squeezed under a long list of controls. Shares state with ElectronicStructurePanel via one
	// ElectronicStructureSession - selecting a band there is reflected here immediately, and vice
	// versa (the split/relative-to-VBM display toggles live here since they only affect this plot).
	class OccupationDiagramPanel final : public IPanel
	{
	public:
		explicit OccupationDiagramPanel(
			Ref<ElectronicStructureSession> session,
			std::string title = "Occupation Diagram",
			bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void renderPlot(
			ElectronicStructureSession::WindowState &state, RendererWindowState &windowState,
			const std::vector<OrbitalRecord> &filtered, float vbm, bool autofitRequested);

		Ref<ElectronicStructureSession> m_Session;
	};
} // namespace DefectStudio
