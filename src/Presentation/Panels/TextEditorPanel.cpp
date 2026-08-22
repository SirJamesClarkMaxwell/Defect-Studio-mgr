#include "Core/dspch.hpp"

#include "Presentation/Panels/TextEditorPanel.hpp"

#include <imgui.h>

#include "IO/TextFileIO.hpp"
#include "Presentation/EditorFonts.hpp"

namespace DefectStudio
{
	TextEditorPanel::TextEditorPanel(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
	}

	Ref<IPanel> TextEditorPanel::Clone() const
	{
		return CreateRef<TextEditorPanel>(*this);
	}

	void TextEditorPanel::OpenFile(const Path &path)
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(path, text, error))
		{
			m_LoadError = "Failed to load: " + error;
			SetVisible(true);
			return;
		}

		m_LoadError.clear();
		m_FilePath = path;
		m_DisplayName = path.filename().string();
		m_Editor.SetText(text);
		m_Dirty = false;
		SetVisible(true);
	}

	void TextEditorPanel::save()
	{
		if (m_FilePath.Empty())
			return;

		std::string error;
		if (TextFileIO::Save(m_FilePath, m_Editor.GetText(), error))
			m_Dirty = false;
		else
			m_LoadError = "Failed to save: " + error;
	}

	void TextEditorPanel::Render()
	{
		if (!IsVisible())
			return;

		// "###TextEditorPanel" pins ImGui's window identity (docking, size/position) regardless of
		// which file is currently open - same pattern as RendererPanel's per-window labels.
		const std::string label =
			(m_DisplayName.empty() ? GetTitle() : m_DisplayName) + (m_Dirty ? " *" : "") + "###TextEditorPanel";

		bool windowOpen = true;
		if (!ImGui::Begin(label.c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (!m_LoadError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_LoadError.c_str());
		}
		else if (m_FilePath.Empty())
		{
			ImGui::TextDisabled("No file open - double-click a file in the project tree.");
		}
		else
		{
			const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			const ImGuiIO &io = ImGui::GetIO();
			if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
				save();

			// ImGuiColorTextEdit assumes a fixed-width font for its cursor/column grid - the app's
			// default UI font is usually proportional, which is what made this look subtly broken
			// (misaligned glyphs, odd spacing) before this pushed a real monospace font around it.
			ImFont *monospaceFont = GetEditorMonospaceFont();
			if (monospaceFont != nullptr)
				ImGui::PushFont(monospaceFont);
			const bool textChanged = m_Editor.Render("TextEditorContent");
			if (monospaceFont != nullptr)
				ImGui::PopFont();
			if (textChanged)
				m_Dirty = true;
		}

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
