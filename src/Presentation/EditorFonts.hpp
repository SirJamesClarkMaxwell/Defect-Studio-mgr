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
	// Font with Greek, sub/superscript digits and math operators (Γ, σ, ₁, ³, ⊕) for panels that show
	// group-theory notation; the default ProggyClean UI font has none of them. Never null after the
	// first atlas build (falls back to the UI font).
	[[nodiscard]] ImFont *GetEditorMathFont();
	void SetEditorMathFont(ImFont *font);
} // namespace DefectStudio
