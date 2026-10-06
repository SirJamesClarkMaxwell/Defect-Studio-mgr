#pragma once

#include <functional>
#include <string_view>
#include <vector>

// A small TeX-like markup for scene text (free labels): V_B, V_{Si}^{-}, E_g^{(1)}, \alpha, 1.54\,\AA.
// Pure - no font, no GL. ParseTexMarkup turns UTF-8 markup into glyphs with a size and a baseline
// shift; LayoutTexGlyphs places them along a line given the font's advances. The MSDF label renderer
// draws the placed glyphs (OpenGlRendererBackend::AppendLabelInstances).
//
// Grammar (anything not listed is literal text, decoded from UTF-8):
//   _x  ^x        next single character or \command is a subscript / superscript
//   _{..} ^{..}   a group is; groups nest (x^{a_{b}})
//   {..}          plain grouping, the braces themselves are not drawn
//   \alpha..\omega, \Alpha..\Omega   Greek (capitals that look Latin map to the Latin letter)
//   \pm \mp \cdot \times \to \rightarrow \leftarrow \leftrightarrow \uparrow \downarrow
//   \circ \deg \AA \infty \approx \neq \leq \geq \sim \prime \hbar \langle \rangle
//   \, (thin space)   \  (space)   \_ \^ \{ \} \\ (the character itself)
//   unknown \command  drawn literally, backslash included
//   '-' inside a sub/superscript is drawn as U+2212 MINUS SIGN (charges: V_N^-)
//   a '_' or '^' with nothing after it, and a '}' with no open group, are literal;
//   a group left open runs to the end of the text
// A script that directly follows a script of the other kind on the same base (E_g^{(1)}) is
// stacked over it: it starts at the same x instead of after it.
namespace DefectStudio
{
	inline constexpr float kTexScriptScale = 0.7f;
	// Baseline shifts in em of the enclosing text (multiplied by its scale and added to its shift).
	inline constexpr float kTexSubscriptShift = -0.25f;
	inline constexpr float kTexSuperscriptShift = 0.42f;

	struct TexGlyph
	{
		char32_t codepoint = U' ';
		float scale = 1.0f;          // product of kTexScriptScale over the nesting depth
		float baselineShift = 0.0f;  // em, relative to the line's baseline
		// First glyph of a script stacked over the directly preceding script (see header comment).
		bool stacksOnPreviousScript = false;
	};

	struct TexPlacedGlyph
	{
		char32_t codepoint = U' ';
		float x = 0.0f;              // em, pen position of the glyph's origin; the line starts at 0
		float baselineShift = 0.0f;  // em
		float scale = 1.0f;
	};

	struct TexLayout
	{
		std::vector<TexPlacedGlyph> glyphs;
		float width = 0.0f;          // em, rightmost pen position reached
	};

	[[nodiscard]] std::vector<TexGlyph> ParseTexMarkup(std::string_view utf8);

	// `advance(codepoint)` = the glyph's advance in em at scale 1 (0 for a glyph the font lacks).
	// Each glyph advances the pen by advance * scale. A stacked script rewinds the pen to where the
	// previous script started and, once done, leaves it at the further of the two scripts' ends.
	[[nodiscard]] TexLayout LayoutTexGlyphs(
		const std::vector<TexGlyph> &glyphs, const std::function<float(char32_t)> &advance);
} // namespace DefectStudio
