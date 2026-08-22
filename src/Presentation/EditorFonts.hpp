#pragma once

struct ImFont;

namespace DefectStudio
{
	// Decoupled font handoff between ImGuiLayer (owns the font atlas, rebuilds it whenever the
	// user changes their UI font in Settings) and any panel that needs a font OTHER than the
	// current app-wide default - currently just the monospace font for TextEditorPanel, which must
	// stay fixed-width for ImGuiColorTextEdit's column grid regardless of what proportional font the
	// user picked for the rest of the UI. A free-function singleton (matching Core/Utils/Input.cpp's
	// backend pattern) avoids threading an ImGuiLayer reference through panel construction for one
	// pointer that's only ever read during Render().
	[[nodiscard]] ImFont *GetEditorMonospaceFont();
	void SetEditorMonospaceFont(ImFont *font);
} // namespace DefectStudio
