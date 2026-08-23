#pragma once

#include <string>
#include <unordered_map>

#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	// Tabular editor over the global per-element color/radius table (AtomStyleTable), distinct from
	// the Periodic Table picker (RendererPanel::drawPeriodicTableWindow - picks one element to act
	// on, doesn't edit style). AtomStyleTable copies share their underlying data (see its own
	// comment), so ReplaceStyles here is visible everywhere the table is already held - the only
	// thing this panel does beyond that call is refresh already-open windows' cached
	// color/radius (they're baked into RendererStructureData at build time, not read live).
	class ElementCatalogPanel final : public IPanel
	{
	public:
		explicit ElementCatalogPanel(
			RendererLayer &layer,
			AtomStyleTable atomStyleTable,
			Path atomStylesPath,
			std::string title = "Element Catalog",
			bool visibleByDefault = false);
		ElementCatalogPanel(const ElementCatalogPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void applyToLiveTable();

		RendererLayer &m_Layer;
		AtomStyleTable m_AtomStyleTable;
		Path m_AtomStylesPath;
		std::unordered_map<std::string, AtomRenderStyle> m_EditBuffer;
		bool m_Loaded = false;
		int m_AddElementIndex = 0;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
