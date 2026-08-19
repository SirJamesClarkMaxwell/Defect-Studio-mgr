#pragma once

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class EventBus;

	// Dockable, resizable "Export Image" panel - filename/path, resolution, visibility toggles,
	// zoom + per-edge crop, and a live preview that fills whatever space the panel is given
	// (aspect-locked to the chosen export resolution, letterboxed rather than stretched).
	// Replaces the old BeginPopupModal version: a normal docked window sidesteps the ImGui
	// popup-ID-stack fragility that a modal has (OpenPopup/BeginPopupModal both hash against
	// "the current window's ID stack at the call site", which broke when triggered from the F12
	// command path - an event handler with no ImGui window context at all). Visibility mirrors
	// RenderExportDialogState::open, which the toolbar button and the F12 command both set.
	class ExportImagePanel final : public IPanel
	{
	public:
		explicit ExportImagePanel(
			RendererLayer &layer,
			Ref<EventBus> eventBus,
			std::string title = "Export Image",
			bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		RendererLayer &m_Layer;
		Ref<EventBus> m_EventBus;
	};
} // namespace DefectStudio
