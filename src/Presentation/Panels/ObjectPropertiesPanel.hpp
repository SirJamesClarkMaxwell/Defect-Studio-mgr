#pragma once

#include <string>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;

	// Properties of the single selected atom in the focused renderer viewport - v1 is just the
	// cartesian position (translate X/Y/Z), committed through the same "renderer.gizmo.commit_transform"
	// command the viewport gizmo drag uses, so undo/redo behaves identically either way. Multi-select
	// editing and per-atom style overrides (docs/work/project/TODO.md "Replanning 2026-08-22") are a
	// separate, later task - not designed here.
	class ObjectPropertiesPanel final : public IPanel
	{
	public:
		explicit ObjectPropertiesPanel(
			RendererLayer &layer,
			WeakRef<CommandRegistry> commandRegistry,
			std::string title = "Object Properties",
			bool visibleByDefault = false);
		ObjectPropertiesPanel(const ObjectPropertiesPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
	};
} // namespace DefectStudio
