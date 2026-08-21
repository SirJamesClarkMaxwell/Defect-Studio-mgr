#include "Core/dspch.hpp"

#include "Renderer/OpenGl/MsdfFont.hpp"

// msdfgen headers use INFINITE as an identifier; <windows.h> (pulled in transitively on this
// platform) #defines it - same guard Hazel's Font.cpp carries.
#undef INFINITE
#include <msdf-atlas-gen.h>

#include <glad/gl.h>

#include "Core/Logging/Logger.hpp"

namespace DefectStudio
{
	struct MsdfFont::Impl
	{
		std::vector<msdf_atlas::GlyphGeometry> glyphs;
		msdf_atlas::FontGeometry fontGeometry{&glyphs};
	};

	namespace
	{
		struct CharsetRange
		{
			std::uint32_t begin;
			std::uint32_t end;
		};

		// Basic Latin + Latin-1 Supplement (same range imgui_draw.cpp defaults to, and what
		// Hazel's Font.cpp loads) - covers digits/letters/punctuation plus U+00B0 "°" and U+00C5
		// "Å", everything a bond-length/angle label needs (e.g. "1.542 Å", "104.5°").
		constexpr CharsetRange kCharsetRanges[] = {{0x0020, 0x00FF}};
	} // namespace

	MsdfFont::MsdfFont(const Path &fontFilePath)
		: m_Impl(std::make_unique<Impl>())
	{
		msdfgen::FreetypeHandle *freetype = msdfgen::initializeFreetype();
		if (freetype == nullptr)
		{
			DS_LOG_WARN("MsdfFont: failed to initialize FreeType");
			return;
		}

		const std::string fontPathString = fontFilePath.Native().string();
		msdfgen::FontHandle *font = msdfgen::loadFont(freetype, fontPathString.c_str());
		if (font == nullptr)
		{
			DS_LOG_WARN("MsdfFont: failed to load font '{}'", fontPathString);
			msdfgen::deinitializeFreetype(freetype);
			return;
		}

		msdf_atlas::Charset charset;
		for (const CharsetRange &range : kCharsetRanges)
			for (std::uint32_t codepoint = range.begin; codepoint <= range.end; ++codepoint)
				charset.add(codepoint);

		constexpr double kFontScale = 1.0;
		const int glyphsLoaded = m_Impl->fontGeometry.loadCharset(font, kFontScale, charset);
		DS_LOG_INFO(
			"MsdfFont: loaded {} glyphs from '{}' ({} requested)", glyphsLoaded, fontPathString, charset.size());

		double emSize = 40.0;
		msdf_atlas::TightAtlasPacker atlasPacker;
		atlasPacker.setPixelRange(m_PixelRange);
		atlasPacker.setMiterLimit(1.0);
		atlasPacker.setPadding(0);
		atlasPacker.setScale(emSize);
		const int unpacked = atlasPacker.pack(m_Impl->glyphs.data(), static_cast<int>(m_Impl->glyphs.size()));
		if (unpacked != 0)
			DS_LOG_WARN("MsdfFont: {} glyph(s) did not fit the atlas", unpacked);

		int atlasWidth = 0;
		int atlasHeight = 0;
		atlasPacker.getDimensions(atlasWidth, atlasHeight);
		emSize = atlasPacker.getScale();

		// Deterministic per-glyph edge coloring (msdfgen's "ink trap" heuristic) - cheap enough to
		// always run single-threaded for a charset this small (~224 glyphs), so this skips Hazel's
		// optional multi-threaded Workload path entirely.
		constexpr double kAngleThreshold = 3.0;
		constexpr unsigned long long kLcgMultiplier = 6364136223846793005ull;
		unsigned long long glyphSeed = 0;
		for (msdf_atlas::GlyphGeometry &glyph : m_Impl->glyphs)
		{
			glyphSeed *= kLcgMultiplier;
			glyph.edgeColoring(msdfgen::edgeColoringInkTrap, kAngleThreshold, glyphSeed);
		}

		msdf_atlas::GeneratorAttributes attributes;
		attributes.config.overlapSupport = true;
		attributes.scanlinePass = true;
		// First template arg is msdfGenerator's own working type (always float, regardless of the
		// final storage type) - the actual output storage type (uint8_t) only appears inside
		// BitmapAtlasStorage below. Mirrors Hazel's Font.cpp CreateAndCacheAtlas<T, S, ...> split.
		msdf_atlas::ImmediateAtlasGenerator<
			float, 3, msdf_atlas::msdfGenerator, msdf_atlas::BitmapAtlasStorage<unsigned char, 3>>
			generator(atlasWidth, atlasHeight);
		generator.setAttributes(attributes);
		generator.setThreadCount(4);
		generator.generate(m_Impl->glyphs.data(), static_cast<int>(m_Impl->glyphs.size()));

		const msdfgen::BitmapConstRef<unsigned char, 3> bitmap =
			static_cast<msdfgen::BitmapConstRef<unsigned char, 3>>(generator.atlasStorage());

		unsigned int textureId = 0;
		glGenTextures(1, &textureId);
		glBindTexture(GL_TEXTURE_2D, textureId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(
			GL_TEXTURE_2D, 0, GL_RGB8, bitmap.width, bitmap.height, 0, GL_RGB, GL_UNSIGNED_BYTE, bitmap.pixels);
		glBindTexture(GL_TEXTURE_2D, 0);

		m_AtlasTextureId = textureId;
		m_AtlasWidth = static_cast<int>(bitmap.width);
		m_AtlasHeight = static_cast<int>(bitmap.height);
		m_EmSize = emSize;
		m_Valid = true;

		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(freetype);
	}

	MsdfFont::~MsdfFont()
	{
		if (m_AtlasTextureId != 0)
			glDeleteTextures(1, &m_AtlasTextureId);
	}

	MsdfGlyphQuad MsdfFont::GetGlyphQuad(char32_t codepoint) const
	{
		MsdfGlyphQuad quad;
		if (!m_Valid)
			return quad;

		const msdf_atlas::GlyphGeometry *glyph =
			m_Impl->fontGeometry.getGlyph(static_cast<msdf_atlas::unicode_t>(codepoint));
		if (glyph == nullptr)
			return quad;

		double planeLeft = 0.0, planeBottom = 0.0, planeRight = 0.0, planeTop = 0.0;
		glyph->getQuadPlaneBounds(planeLeft, planeBottom, planeRight, planeTop);
		double atlasLeft = 0.0, atlasBottom = 0.0, atlasRight = 0.0, atlasTop = 0.0;
		glyph->getQuadAtlasBounds(atlasLeft, atlasBottom, atlasRight, atlasTop);

		quad.found = true;
		quad.planeMin = glm::vec2(static_cast<float>(planeLeft), static_cast<float>(planeBottom));
		quad.planeMax = glm::vec2(static_cast<float>(planeRight), static_cast<float>(planeTop));
		quad.atlasUvMin = glm::vec2(
			static_cast<float>(atlasLeft / m_AtlasWidth), static_cast<float>(atlasBottom / m_AtlasHeight));
		quad.atlasUvMax = glm::vec2(
			static_cast<float>(atlasRight / m_AtlasWidth), static_cast<float>(atlasTop / m_AtlasHeight));
		quad.advance = static_cast<float>(glyph->getAdvance());
		return quad;
	}
} // namespace DefectStudio
