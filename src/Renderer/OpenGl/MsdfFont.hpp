#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"

// Forward-declared rather than #include "FontGeometry.h" here: that header (and everything under
// Vendor/msdf-atlas-gen) pulls in msdfgen/freetype types we don't want leaking into every TU that
// includes this header. Full definition only needed in MsdfFont.cpp.
namespace msdf_atlas
{
	class FontGeometry;
	class GlyphGeometry;
} // namespace msdf_atlas

namespace DefectStudio
{
	// Per-glyph layout data needed to place one character's quad: local-space plane bounds (where
	// the quad sits relative to the pen position, in em units) and its UV rect in the atlas.
	struct MsdfGlyphQuad
	{
		bool found = false;
		glm::vec2 planeMin = glm::vec2(0.0f);
		glm::vec2 planeMax = glm::vec2(0.0f);
		glm::vec2 atlasUvMin = glm::vec2(0.0f);
		glm::vec2 atlasUvMax = glm::vec2(0.0f);
		float advance = 0.0f;
	};

	// Runtime MSDF atlas generator/holder for one font - ported from TheCherno/Hazel's
	// Hazel/src/Hazel/Renderer/Font.{h,cpp} and MSDFData.h (Apache 2.0; see SceneComponents.hpp
	// for this repo's other Hazel-derived file), adapted from Hazel's Texture2D/Ref<> abstractions
	// to this repo's raw-GLuint texture convention (matches RendererLayer's toolbar icon loading)
	// and DefectStudio::Path. The atlas-generation call sequence (initializeFreetype -> loadFont ->
	// FontGeometry::loadCharset -> TightAtlasPacker -> ImmediateAtlasGenerator -> edge coloring) is
	// carried over near-verbatim - that sequence is the genuinely non-obvious part of using
	// msdf-atlas-gen, not something worth re-deriving from the API docs.
	class MsdfFont
	{
	public:
		explicit MsdfFont(const Path &fontFilePath);
		~MsdfFont();

		MsdfFont(const MsdfFont &) = delete;
		MsdfFont &operator=(const MsdfFont &) = delete;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Valid;
		}

		[[nodiscard]] unsigned int AtlasTextureId() const noexcept
		{
			return m_AtlasTextureId;
		}

		// Em-square size the atlas was packed at (TightAtlasPacker::getScale()) - glyph plane
		// bounds/advances from GetGlyphQuad are in em units, multiply by the caller's desired
		// world-space font size, not by this.
		[[nodiscard]] double EmSize() const noexcept
		{
			return m_EmSize;
		}

		// Signed distance field pixel range the atlas was generated with (TightAtlasPacker::
		// setPixelRange) - the shader needs this to convert the sampled median-channel distance
		// from atlas-texel units into screen-space pixels for the smoothstep width.
		[[nodiscard]] double PixelRange() const noexcept
		{
			return m_PixelRange;
		}

		[[nodiscard]] MsdfGlyphQuad GetGlyphQuad(char32_t codepoint) const;

	private:
		bool m_Valid = false;
		unsigned int m_AtlasTextureId = 0;
		int m_AtlasWidth = 0;
		int m_AtlasHeight = 0;
		double m_EmSize = 1.0;
		double m_PixelRange = 2.0;
		// Owns the glyph/font geometry data queried by GetGlyphQuad - pimpl'd (see .cpp) purely to
		// keep msdf_atlas::FontGeometry/GlyphGeometry out of this header.
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} // namespace DefectStudio
