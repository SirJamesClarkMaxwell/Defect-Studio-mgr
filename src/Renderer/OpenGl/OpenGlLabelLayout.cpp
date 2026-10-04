#include "Core/dspch.hpp"
#include "Renderer/OpenGl/OpenGlLabelLayout.hpp"
#include <cstdio>
namespace DefectStudio
{
	// snprintf keeps this ASCII-only by construction (digits/'.'/space), so the byte->char32_t
	// widen below is exact - no UTF-8 decoding needed. U+00C5 (the Angstrom sign, same codepoint
	// as Latin capital A with ring above) is a literal U+00C5 char32_t below, relying on this file
	// being read as UTF-8 - already required repo-wide (premake sets /utf-8 for MSVC).
	[[nodiscard]] std::u32string FormatBondLengthLabel(float lengthAngstrom)
	{
		char buffer[16];
		const int written = std::snprintf(buffer, sizeof(buffer), "%.2f ", static_cast<double>(lengthAngstrom));

		std::u32string text;
		if (written > 0)
		{
			text.reserve(static_cast<std::size_t>(written) + 1);
			for (int i = 0; i < written; ++i)
				text.push_back(static_cast<char32_t>(static_cast<unsigned char>(buffer[i])));
		}
		text.push_back(U'Å');
		return text;
	}

	[[nodiscard]] std::u32string FormatAngleLabel(float angleDeg)
	{
		char buffer[16];
		const int written = std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(angleDeg));

		std::u32string text;
		if (written > 0)
		{
			text.reserve(static_cast<std::size_t>(written) + 1);
			for (int i = 0; i < written; ++i)
				text.push_back(static_cast<char32_t>(static_cast<unsigned char>(buffer[i])));
		}
		text.push_back(U'°'); // degree sign
		return text;
	}

	std::vector<TexGlyph> PlainGlyphs(const std::u32string &text)
	{
		std::vector<TexGlyph> glyphs;
		for (char32_t cp : text) glyphs.push_back({cp});
		return glyphs;
	}

	// Shared by every label call site below - lays out one string's glyph quads (pen-advance +
	// centering) around `worldCenter` and appends them to whichever instance list the caller is
	// building. Returns the label's local bounding box for AppendLabelBackgroundInstance.
	LabelLocalBounds AppendLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &worldCenter,
		const std::vector<TexGlyph> &text,
		std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style,
		float rotationRadians,
		bool selected)
	{
		// World-space label height (em units -> world units) and a rough baseline centering
		// offset (typical glyph ascent/descent split) - tuned by eye, not derived from font
		// metrics, good enough for a fixed-purpose label rather than general text layout.
		constexpr float kWorldFontSize = 0.28f;
		constexpr float kBaselineOffset = -0.35f * kWorldFontSize;

		const TexLayout layout = LayoutTexGlyphs(text, [&](char32_t cp) { return font.GetGlyphQuad(cp).advance; });

		const glm::vec4 textColor(style.textColor, style.textAlpha);
		LabelLocalBounds bounds;
		for (const TexPlacedGlyph &placed : layout.glyphs)
		{
			const MsdfGlyphQuad glyph = font.GetGlyphQuad(placed.codepoint);
			const float penX = (placed.x - layout.width * 0.5f) * kWorldFontSize;
			const float baseline = kBaselineOffset + placed.baselineShift * kWorldFontSize;
			const float size = placed.scale * kWorldFontSize;
			if (glyph.found && glyph.planeMax.x > glyph.planeMin.x && glyph.planeMax.y > glyph.planeMin.y)
			{
				const glm::vec2 glyphMin(
					penX + glyph.planeMin.x * size, baseline + glyph.planeMin.y * size);
				const glm::vec2 glyphMax(
					penX + glyph.planeMax.x * size, baseline + glyph.planeMax.y * size);
				if (!bounds.hasBounds)
				{
					bounds.min = glyphMin;
					bounds.max = glyphMax;
					bounds.hasBounds = true;
				}
				else
				{
					bounds.min = glm::min(bounds.min, glyphMin);
					bounds.max = glm::max(bounds.max, glyphMax);
				}

				OpenGlLabelInstance instance;
				instance.worldCenter = worldCenter;
				instance.localOffsetSize = style.scale * glm::vec4(glyphMin, glyphMax - glyphMin);
				instance.atlasUvMinMax = glm::vec4(glyph.atlasUvMin, glyph.atlasUvMax);
				instance.color = textColor;
				instance.rotationRadians = rotationRadians;
				instance.selected = selected ? 1.0f : 0.0f;
				// outlineColor/outlineWidth/cornerRadius stay zero-initialized here - they style the
				// background quad's frame (see AppendLabelBackgroundInstance below), not glyphs.
				instance.strokeColor = style.strokeColor;
				instance.strokeWidth = style.strokeWidth;
				outInstances.push_back(instance);
			}
		}
		return bounds;
	}

	// LabelStyle::backgroundAlpha <= 0 (the default) or empty text (no bounds) - no-op, so call
	// sites can invoke this unconditionally right after AppendLabelInstances without their own guard.
	void AppendLabelBackgroundInstance(
		const glm::vec3 &worldCenter,
		const LabelLocalBounds &bounds,
		const RendererWindowState::LabelStyle &style,
		float rotationRadians,
		std::vector<OpenGlLabelInstance> &outInstances,
		bool selected)
	{
		if (style.backgroundAlpha <= 0.0f || !bounds.hasBounds)
			return;

		const glm::vec2 paddedMin = bounds.min - style.padding;
		const glm::vec2 paddedMax = bounds.max + style.padding;
		OpenGlLabelInstance instance;
		instance.worldCenter = worldCenter;
		instance.localOffsetSize = style.scale * glm::vec4(paddedMin, paddedMax - paddedMin);
		instance.color = glm::vec4(style.backgroundColor, style.backgroundAlpha);
		instance.rotationRadians = rotationRadians;
		instance.outlineColor = style.outlineColor;
		// Scaled the same way the box itself is (style.scale), so the border/corner radius grow and
		// shrink in proportion to the label instead of staying a fixed size while the box scales.
		instance.outlineWidth = style.scale * style.outlineWidth;
		instance.cornerRadius = style.scale * style.cornerRadius;
		instance.selected = selected ? 1.0f : 0.0f;
		outInstances.push_back(instance);
	}

	LabelLocalBounds AppendBondLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &midpoint,
		float lengthAngstrom,
		std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style,
		float rotationRadians,
		bool selected)
	{
		return AppendLabelInstances(font, midpoint, PlainGlyphs(FormatBondLengthLabel(lengthAngstrom)), outInstances, style, rotationRadians, selected);
	}

	LabelLocalBounds AppendAngleLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &vertex,
		float angleDeg,
		std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style,
		float rotationRadians,
		bool selected)
	{
		return AppendLabelInstances(font, vertex, PlainGlyphs(FormatAngleLabel(angleDeg)), outInstances, style, rotationRadians, selected);
	}

}
