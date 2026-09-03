#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "IO/MaterialLibraryIO.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class DomainLayer;

	// Browses and edits the two material libraries - the active project's and the user's personal
	// one - and opens any entry as a renderer window. Both scopes are the same MaterialLibraryIO
	// against different files; the split exists so a structure worth keeping across projects does
	// not have to be copied into each one.
	class MaterialsCollectionPanel final : public IPanel
	{
	public:
		explicit MaterialsCollectionPanel(
			RendererLayer &rendererLayer,
			WeakRef<DomainLayer> domainLayer,
			ElementPropertiesTable elementPropertiesTable,
			AtomStyleTable atomStyleTable,
			Path personalLibraryPath,
			std::string title = "Materials Collection",
			bool visibleByDefault = false);
		MaterialsCollectionPanel(const MaterialsCollectionPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Pushed by EditorLayer whenever the active project changes. An empty path means no project
		// is open, which the "This Project" tab reports instead of pointing at a stale library.
		void SetProjectLibraryPath(Path projectLibraryPath);

	private:
		enum class Scope
		{
			Project,
			Personal,
		};

		// Every MaterialLibraryIO call spawns a Python subprocess, so the listing is cached and
		// refreshed only on events that can actually change it (scope switch, add, delete, explicit
		// Refresh) - never per frame, which would fork ase.db on every repaint.
		struct ScopeState
		{
			std::vector<MaterialLibraryEntry> entries;
			std::string error;
			bool loaded = false;
		};

		[[nodiscard]] Path libraryPathFor(Scope scope) const;
		[[nodiscard]] ScopeState &stateFor(Scope scope);
		void reload(Scope scope);
		void drawScopeTab(Scope scope);
		void drawSaveCurrentPopup();
		void drawDeleteConfirmPopup();

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		ElementPropertiesTable m_ElementPropertiesTable;
		AtomStyleTable m_AtomStyleTable;

		Path m_PersonalLibraryPath;
		Path m_ProjectLibraryPath;

		ScopeState m_ProjectState;
		ScopeState m_PersonalState;

		std::array<char, 128> m_SaveNameBuffer{};
		std::array<char, 256> m_SaveNotesBuffer{};
		Scope m_SaveScope = Scope::Project;
		std::string m_SaveError;
		bool m_SavePopupOpen = false;

		Scope m_DeleteScope = Scope::Project;
		std::string m_DeleteEntryId;
		std::string m_DeleteEntryName;
		bool m_DeletePopupOpen = false;

		std::string m_StatusMessage;
	};
} // namespace DefectStudio
