#pragma once

#include <string>

#include <TextEditor.h>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	// Single dockable text editor for in-project files (INCAR, run_computations scripts, ...) -
	// one panel instance, no tabs: opening a new file replaces the current one. POSCAR/CONTCAR keep
	// their existing structure-viewer path (ProjectTreePanel's RMB "Open Defect"); this is for
	// everything else, opened by double-clicking any leaf in the project tree.
	class TextEditorPanel final : public IPanel
	{
	public:
		explicit TextEditorPanel(std::string title = "Text Editor", bool visibleByDefault = false);
		TextEditorPanel(const TextEditorPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Loads path's contents into the editor, replacing whatever was open (no unsaved-changes
		// prompt - MVP scope, see docs/work/project/TODO.md). Makes the panel visible.
		void OpenFile(const Path &path);

	private:
		void save();

		TextEditor m_Editor;
		Path m_FilePath;
		std::string m_DisplayName;
		std::string m_LoadError;
		bool m_Dirty = false;
	};
} // namespace DefectStudio
