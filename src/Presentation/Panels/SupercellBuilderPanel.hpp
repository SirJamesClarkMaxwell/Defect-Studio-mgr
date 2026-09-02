#pragma once

#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class JobSystem;
	class SuggestSupercellMatrixJob;

	// Expands the focused viewport's structure into a supercell and opens the result as its own
	// window, leaving the source unit cell open beside it. Three input modes write to one shared
	// SupercellMatrix, so switching tabs never loses what was already typed: Simple (diagonal
	// N x M x K), Matrix (full 3x3, for shears and non-diagonal cells from papers), and Surface
	// (Miller indices + layer count, resolved through ASE off the main thread).
	class SupercellBuilderPanel final : public IPanel
	{
	public:
		explicit SupercellBuilderPanel(
			RendererLayer &rendererLayer,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<JobSystem> jobSystem,
			ElementPropertiesTable elementPropertiesTable,
			AtomStyleTable atomStyleTable,
			std::string title = "Supercell Builder",
			bool visibleByDefault = false);
		SupercellBuilderPanel(const SupercellBuilderPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void drawSimpleTab();
		void drawMatrixTab();
		void drawSurfaceTab();
		void dispatchSurfaceSuggestion(const CrystalStructure &unitCell);
		void pollSurfaceJob();

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;

		// Reseeded to identity whenever the focused window changes - a matrix typed for one cell is
		// meaningless for another, and silently reusing it would generate a wrong supercell from a
		// control that looks correct.
		std::string m_EditedForWindowId;
		SupercellMatrix m_EditedMatrix;

		MillerIndices m_SurfaceHkl;
		int m_SurfaceLayers = 2;

		Ref<SuggestSupercellMatrixJob> m_PendingSurfaceJob;
		JobId m_PendingSurfaceJobId = 0;
		std::string m_SurfaceError;

		std::string m_StatusMessage;
	};
} // namespace DefectStudio
