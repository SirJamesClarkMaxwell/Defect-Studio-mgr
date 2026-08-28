#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glad/gl.h>
#include <stb_image_write.h>

#include <cstdio>

#include "Core/Logging/Logger.hpp"
#include "Core/Platform/PlatformPaths.hpp"
#include "Renderer/RendererViewCamera.hpp"

#if defined(TRACY_ENABLE)
#include <tracy/TracyOpenGL.hpp>
#endif

namespace DefectStudio
{
	struct BondComputeInput
	{
		glm::vec4 start = glm::vec4(0.0f);
		glm::vec4 finish = glm::vec4(0.0f);
		glm::vec4 colorA = glm::vec4(1.0f);
		glm::vec4 colorB = glm::vec4(1.0f);
		float radius = 0.0f;
		float pad0 = 0.0f;
		float pad1 = 0.0f;
		float pad2 = 0.0f;
	};

	struct BondComputeOutput
	{
		glm::mat4 transform = glm::mat4(1.0f);
		glm::vec4 colorA = glm::vec4(1.0f);
		glm::vec4 colorB = glm::vec4(1.0f);
	};

	struct SphereVertex
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
	};

	struct CylinderVertex
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
		float gradientT = 0.0f;
	};

	[[nodiscard]] static bool IsFiniteVec3(const glm::vec3 &value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	static void GLAPIENTRY OpenGlDebugCallback(
		unsigned int source,
		unsigned int type,
		unsigned int id,
		unsigned int severity,
		int length,
		const char *message,
		const void *userParam)
	{
		(void)source;
		(void)length;
		(void)userParam;
		if (message == nullptr)
			return;

		if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
			return;

		const bool isKnownPerformanceSpam = type == GL_DEBUG_TYPE_PERFORMANCE && (id == 131185u || id == 131218u);
		if (isKnownPerformanceSpam)
			return;

		if (severity == GL_DEBUG_SEVERITY_HIGH)
		{
			DS_LOG_ERROR("OpenGL debug [id={} type={} severity={}]: {}", id, type, severity, message);
			return;
		}
		if (severity == GL_DEBUG_SEVERITY_MEDIUM)
		{
			DS_LOG_WARN("OpenGL debug [id={} type={} severity={}]: {}", id, type, severity, message);
			return;
		}

		DS_LOG_DEBUG("OpenGL debug [id={} type={} severity={}]: {}", id, type, severity, message);
	}

	[[nodiscard]] StructuredError MakeMeshAssetError(
		std::string code,
		std::string userMessage,
		std::string technicalDetails)
	{
		return StructuredError{
			ErrorCategory::Validation,
			Severity::Error,
			std::move(userMessage),
			std::move(technicalDetails),
			"Fix renderer primitive mesh assets.",
			"OpenGlRendererBackend",
			std::move(code)};
	}

	// Same deploy-path/dev-tree-fallback pattern as RendererLayer::resolveShaderDirectory (Etap 0) -
	// reuses the app's own already-shipped UI font rather than adding a new font asset just for
	// labels.
	[[nodiscard]] Path resolveLabelFontPath()
	{
		const Path executableDirectory = Platform::GetExecutableDirectory();
		if (!executableDirectory.Empty())
		{
			const Path deployFont = Path::FromResolved(
				executableDirectory.Native() / "install" / "app" / "assets" / "fonts" / "segoeui.ttf");
			if (FileSystem::Exists(deployFont.Native()))
				return deployFont;
		}
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "app" / "assets" / "fonts" / "segoeui.ttf");
	}

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

	// Free-label text comes from an ImGui InputText buffer - plain byte-widening (same approach
	// FormatBondLengthLabel/FormatAngleLabel already use for their glyphs), not real UTF-8 decoding.
	// Correct for the ASCII/Latin-1 range the MSDF atlas's charset actually covers; a multi-byte
	// UTF-8 codepoint would come out as several wrong glyphs instead of one - not attempting that
	// here since v1's label text is expected to be short ASCII call-outs.
	[[nodiscard]] std::u32string ToU32String(const std::string &text)
	{
		std::u32string result;
		result.reserve(text.size());
		for (const char character : text)
			result.push_back(static_cast<char32_t>(static_cast<unsigned char>(character)));
		return result;
	}

	// Matches kSelectionHighlightColor used for atom selection below - same accent, reused here so
	// a selected pinned measurement reads as "selected" the same way a selected atom does. Not part
	// of LabelStyle - it's a transient render-time override, not a persisted per-label style.
	constexpr glm::vec3 kPinnedLabelSelectedColor(0.91f, 0.52f, 0.02f);

	// Local-space (pre style.scale) bounding box of one label's glyphs, for sizing its optional
	// background quad (AppendLabelBackgroundInstance) - hasBounds stays false for empty/all-
	// whitespace text, which the caller treats as "no background to draw".
	struct LabelLocalBounds
	{
		glm::vec2 min = glm::vec2(0.0f);
		glm::vec2 max = glm::vec2(0.0f);
		bool hasBounds = false;
	};

	// Shared by every label call site below - lays out one string's glyph quads (pen-advance +
	// centering) around `worldCenter` and appends them to whichever instance list the caller is
	// building. Returns the label's local bounding box for AppendLabelBackgroundInstance.
	LabelLocalBounds AppendLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &worldCenter,
		const std::u32string &text,
		std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style = {},
		float rotationRadians = 0.0f)
	{
		// World-space label height (em units -> world units) and a rough baseline centering
		// offset (typical glyph ascent/descent split) - tuned by eye, not derived from font
		// metrics, good enough for a fixed-purpose label rather than general text layout.
		constexpr float kWorldFontSize = 0.28f;
		constexpr float kBaselineOffset = -0.35f * kWorldFontSize;

		float totalAdvance = 0.0f;
		for (const char32_t codepoint : text)
			totalAdvance += font.GetGlyphQuad(codepoint).advance;

		const glm::vec4 textColor(style.textColor, style.textAlpha);
		LabelLocalBounds bounds;
		float penX = -totalAdvance * 0.5f * kWorldFontSize;
		for (const char32_t codepoint : text)
		{
			const MsdfGlyphQuad glyph = font.GetGlyphQuad(codepoint);
			if (glyph.found && glyph.planeMax.x > glyph.planeMin.x && glyph.planeMax.y > glyph.planeMin.y)
			{
				const glm::vec2 glyphMin(
					penX + glyph.planeMin.x * kWorldFontSize, kBaselineOffset + glyph.planeMin.y * kWorldFontSize);
				const glm::vec2 glyphMax(
					penX + glyph.planeMax.x * kWorldFontSize, kBaselineOffset + glyph.planeMax.y * kWorldFontSize);
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
				// outlineColor/outlineWidth/cornerRadius stay zero-initialized here - they style the
				// background quad's frame (see AppendLabelBackgroundInstance below), not glyphs.
				instance.strokeColor = style.strokeColor;
				instance.strokeWidth = style.strokeWidth;
				outInstances.push_back(instance);
			}
			penX += glyph.advance * kWorldFontSize;
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
		std::vector<OpenGlLabelInstance> &outInstances)
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
		outInstances.push_back(instance);
	}

	LabelLocalBounds AppendBondLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &midpoint,
		float lengthAngstrom,
		std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style = {},
		float rotationRadians = 0.0f)
	{
		return AppendLabelInstances(font, midpoint, FormatBondLengthLabel(lengthAngstrom), outInstances, style, rotationRadians);
	}

	LabelLocalBounds AppendAngleLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &vertex,
		float angleDeg,
		std::vector<OpenGlLabelInstance> &outInstances,
		const RendererWindowState::LabelStyle &style = {},
		float rotationRadians = 0.0f)
	{
		return AppendLabelInstances(font, vertex, FormatAngleLabel(angleDeg), outInstances, style, rotationRadians);
	}

	[[nodiscard]] glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
	{
		const float length = glm::length(value);
		if (!std::isfinite(length) || length <= 0.00001f)
			return fallback;
		return value / length;
	}

	[[nodiscard]] glm::vec2 SafeNormalize(const glm::vec2 &value, const glm::vec2 &fallback)
	{
		const float length = glm::length(value);
		if (!std::isfinite(length) || length <= 0.00001f)
			return fallback;
		return value / length;
	}

	[[nodiscard]] bool ValidateIndexRange(
		const std::vector<std::uint32_t> &indices,
		std::size_t vertexCount,
		std::string &outError)
	{
		for (const std::uint32_t index : indices)
		{
			if (index < vertexCount)
				continue;
			outError = "index out of range: " + std::to_string(index)
				+ " >= " + std::to_string(vertexCount);
			return false;
		}
		return true;
	}

	struct RefinedSphereMesh
	{
		std::vector<glm::vec3> positions;
		std::vector<std::uint32_t> indices;
	};

	struct RefinedCylinderMesh
	{
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<float> gradientT;
		std::vector<std::uint32_t> indices;
	};

	void AppendRefinedSphereTriangle(
		RefinedSphereMesh &mesh,
		const glm::vec3 &a,
		const glm::vec3 &b,
		const glm::vec3 &c,
		int remainingSubdivisions)
	{
		const glm::vec3 normalizedA = SafeNormalize(a, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 normalizedB = SafeNormalize(b, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 normalizedC = SafeNormalize(c, glm::vec3(0.0f, 1.0f, 0.0f));

		if (remainingSubdivisions <= 0)
		{
			const std::uint32_t baseIndex = static_cast<std::uint32_t>(mesh.positions.size());
			mesh.positions.push_back(normalizedA);
			mesh.positions.push_back(normalizedB);
			mesh.positions.push_back(normalizedC);
			mesh.indices.push_back(baseIndex);
			mesh.indices.push_back(baseIndex + 1u);
			mesh.indices.push_back(baseIndex + 2u);
			return;
		}

		const glm::vec3 ab = SafeNormalize(normalizedA + normalizedB, normalizedA);
		const glm::vec3 bc = SafeNormalize(normalizedB + normalizedC, normalizedB);
		const glm::vec3 ca = SafeNormalize(normalizedC + normalizedA, normalizedC);

		AppendRefinedSphereTriangle(mesh, normalizedA, ab, ca, remainingSubdivisions - 1);
		AppendRefinedSphereTriangle(mesh, normalizedB, bc, ab, remainingSubdivisions - 1);
		AppendRefinedSphereTriangle(mesh, normalizedC, ca, bc, remainingSubdivisions - 1);
		AppendRefinedSphereTriangle(mesh, ab, bc, ca, remainingSubdivisions - 1);
	}

	[[nodiscard]] RefinedSphereMesh BuildRefinedSphereMesh(const RendererStaticMeshData &meshData)
	{
		constexpr int SubdivisionLevels = 4;

		RefinedSphereMesh mesh;
		mesh.positions.reserve(meshData.indices.size() * 16u);
		mesh.indices.reserve(meshData.indices.size() * 16u);

		for (std::size_t index = 0; index + 2u < meshData.indices.size(); index += 3u)
		{
			const glm::vec3 &a = meshData.positions[meshData.indices[index]];
			const glm::vec3 &b = meshData.positions[meshData.indices[index + 1u]];
			const glm::vec3 &c = meshData.positions[meshData.indices[index + 2u]];
			AppendRefinedSphereTriangle(mesh, a, b, c, SubdivisionLevels);
		}

		return mesh;
	}

	[[nodiscard]] RefinedCylinderMesh BuildRefinedCylinderMesh(const RendererStaticMeshData &meshData)
	{
		constexpr std::uint32_t SegmentCount = 96u;
		constexpr float TwoPi = 6.283185307f;

		float minZ = meshData.positions.front().z;
		float maxZ = meshData.positions.front().z;
		float radius = 0.0f;
		for (const glm::vec3 &position : meshData.positions)
		{
			minZ = std::min(minZ, position.z);
			maxZ = std::max(maxZ, position.z);
			radius = std::max(radius, glm::length(glm::vec2(position.x, position.y)));
		}
		if (radius <= 0.0001f)
			radius = 1.0f;
		if (maxZ - minZ <= 0.0001f)
			maxZ = minZ + 1.0f;

		RefinedCylinderMesh mesh;
		mesh.positions.reserve(static_cast<std::size_t>(SegmentCount) * 4u + 2u);
		mesh.normals.reserve(static_cast<std::size_t>(SegmentCount) * 4u + 2u);
		mesh.gradientT.reserve(static_cast<std::size_t>(SegmentCount) * 4u + 2u);
		mesh.indices.reserve(static_cast<std::size_t>(SegmentCount) * 12u);

		for (std::uint32_t segment = 0; segment < SegmentCount; ++segment)
		{
			const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(SegmentCount);
			const glm::vec3 normal(std::cos(angle), std::sin(angle), 0.0f);

			mesh.positions.emplace_back(normal.x * radius, normal.y * radius, minZ);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(0.0f);
			mesh.positions.emplace_back(normal.x * radius, normal.y * radius, maxZ);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(1.0f);

			mesh.positions.emplace_back(normal.x * radius, normal.y * radius, minZ);
			mesh.normals.emplace_back(0.0f, 0.0f, -1.0f);
			mesh.gradientT.push_back(0.0f);
			mesh.positions.emplace_back(normal.x * radius, normal.y * radius, maxZ);
			mesh.normals.emplace_back(0.0f, 0.0f, 1.0f);
			mesh.gradientT.push_back(1.0f);
		}

		const std::uint32_t bottomCenter = static_cast<std::uint32_t>(mesh.positions.size());
		mesh.positions.emplace_back(0.0f, 0.0f, minZ);
		mesh.normals.emplace_back(0.0f, 0.0f, -1.0f);
		mesh.gradientT.push_back(0.0f);
		const std::uint32_t topCenter = static_cast<std::uint32_t>(mesh.positions.size());
		mesh.positions.emplace_back(0.0f, 0.0f, maxZ);
		mesh.normals.emplace_back(0.0f, 0.0f, 1.0f);
		mesh.gradientT.push_back(1.0f);
		(void)bottomCenter;
		(void)topCenter;

		for (std::uint32_t segment = 0; segment < SegmentCount; ++segment)
		{
			const std::uint32_t next = (segment + 1u) % SegmentCount;
			const std::uint32_t bottom = segment * 4u;
			const std::uint32_t top = bottom + 1u;
			const std::uint32_t bottomCap = bottom + 2u;
			const std::uint32_t topCap = bottom + 3u;
			const std::uint32_t nextBottom = next * 4u;
			const std::uint32_t nextTop = nextBottom + 1u;
			const std::uint32_t nextBottomCap = nextBottom + 2u;
			const std::uint32_t nextTopCap = nextBottom + 3u;
			(void)bottomCap;
			(void)topCap;
			(void)nextBottomCap;
			(void)nextTopCap;

			mesh.indices.push_back(bottom);
			mesh.indices.push_back(top);
			mesh.indices.push_back(nextTop);
			mesh.indices.push_back(bottom);
			mesh.indices.push_back(nextTop);
			mesh.indices.push_back(nextBottom);

		}

		return mesh;
	}

	struct RefinedConeMesh
	{
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<float> gradientT;
		std::vector<std::uint32_t> indices;
	};

	// Procedural smooth cone from the coarse cone.obj's bounds only (radius/height), same trick
	// BuildRefinedCylinderMesh above uses - the asset just gates "a valid cone-shaped mesh exists",
	// the actual triangle data comes from here. Base ring at z=minZ, apex at z=maxZ. Lateral normal
	// at angle theta is normalize(cos theta, sin theta, radius/height) for every z along that
	// generatrix (a real cone's side normal doesn't vary with z - see the tangent-plane derivation
	// this was checked against), shared by the base and apex copy of each column exactly like the
	// cylinder's per-column radial normal above.
	// includeBaseCap=false (SceneArrow's only caller, m_ConeMesh) - a flat cap plugging the base ring
	// looked right when the shaft ended exactly at that plane, but showed as a visible disc
	// ("suction cup") sitting on top of the shaft; the fix is to skip the cap and instead let the
	// shaft extend slightly INTO the cone's (now open) base by a geometry-limited overlap (see
	// renderSceneArrows) - the cone's own lateral surface, wider than the shaft near its base, then
	// hides the shaft's open end from every side angle without ever needing a cap plane at all.
	[[nodiscard]] RefinedConeMesh BuildRefinedConeMesh(const RendererStaticMeshData &meshData, bool includeBaseCap)
	{
		constexpr std::uint32_t SegmentCount = 32u;
		constexpr float TwoPi = 6.283185307f;

		float minZ = meshData.positions.front().z;
		float maxZ = meshData.positions.front().z;
		float radius = 0.0f;
		for (const glm::vec3 &position : meshData.positions)
		{
			minZ = std::min(minZ, position.z);
			maxZ = std::max(maxZ, position.z);
			radius = std::max(radius, glm::length(glm::vec2(position.x, position.y)));
		}
		if (radius <= 0.0001f)
			radius = 1.0f;
		if (maxZ - minZ <= 0.0001f)
			maxZ = minZ + 1.0f;
		const float height = maxZ - minZ;

		const std::uint32_t verticesPerSegment = includeBaseCap ? 3u : 2u;

		RefinedConeMesh mesh;
		mesh.positions.reserve(static_cast<std::size_t>(SegmentCount) * verticesPerSegment + 1u);
		mesh.normals.reserve(static_cast<std::size_t>(SegmentCount) * verticesPerSegment + 1u);
		mesh.gradientT.reserve(static_cast<std::size_t>(SegmentCount) * verticesPerSegment + 1u);
		mesh.indices.reserve(static_cast<std::size_t>(SegmentCount) * (includeBaseCap ? 6u : 3u));

		for (std::uint32_t segment = 0; segment < SegmentCount; ++segment)
		{
			const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(SegmentCount);
			const float cosAngle = std::cos(angle);
			const float sinAngle = std::sin(angle);
			const glm::vec3 slantNormal =
				SafeNormalize(glm::vec3(cosAngle, sinAngle, radius / height), glm::vec3(0.0f, 0.0f, 1.0f));

			mesh.positions.emplace_back(cosAngle * radius, sinAngle * radius, minZ); // base (side)
			mesh.normals.push_back(slantNormal);
			mesh.gradientT.push_back(0.0f);
			mesh.positions.emplace_back(0.0f, 0.0f, maxZ); // apex (side)
			mesh.normals.push_back(slantNormal);
			mesh.gradientT.push_back(1.0f);
			if (includeBaseCap)
			{
				mesh.positions.emplace_back(cosAngle * radius, sinAngle * radius, minZ); // base (cap)
				mesh.normals.emplace_back(0.0f, 0.0f, -1.0f);
				mesh.gradientT.push_back(0.0f);
			}
		}

		std::uint32_t baseCenter = 0;
		if (includeBaseCap)
		{
			baseCenter = static_cast<std::uint32_t>(mesh.positions.size());
			mesh.positions.emplace_back(0.0f, 0.0f, minZ);
			mesh.normals.emplace_back(0.0f, 0.0f, -1.0f);
			mesh.gradientT.push_back(0.0f);
		}

		for (std::uint32_t segment = 0; segment < SegmentCount; ++segment)
		{
			const std::uint32_t next = (segment + 1u) % SegmentCount;
			const std::uint32_t base = segment * verticesPerSegment;
			const std::uint32_t apex = base + 1u;
			const std::uint32_t nextBase = next * verticesPerSegment;

			mesh.indices.push_back(base);
			mesh.indices.push_back(apex);
			mesh.indices.push_back(nextBase);

			if (includeBaseCap)
			{
				const std::uint32_t capBase = base + 2u;
				const std::uint32_t nextCapBase = nextBase + 2u;
				mesh.indices.push_back(baseCenter);
				mesh.indices.push_back(nextCapBase);
				mesh.indices.push_back(capBase);
			}
		}

		return mesh;
	}

	// Composes SceneArrow's Arrow3D shaft+head into ONE mesh instead of two separately-instanced draw
	// calls that share no vertices (the earlier capless-cone-plus-overlap approach could hide the gap
	// between them but could never make the *shading* continuous across it). Three segments, each
	// with its own normal (no blending across a segment's own boundary, same "duplicate the vertex,
	// vary only the normal" trick BuildRefinedConeMesh already uses at its own cap/apex):
	//   1) shaft - a plain cylinder (constant shaftRadius, radial normal).
	//   2) shoulder - the shaft/head transition, shape controlled by bulgeStrength (Settings >
	//      Renderer > Scene arrows > Head bulge strength, 0..1): at 0 this is a flat annular disc
	//      (shaftRadius -> headRadius at the same Z, the classic sharp corner every arrow with
	//      headWidth > shaftWidth has at its shoulder); above 0 it becomes a handful of rings spread
	//      over bulgeStrength * kMaxBulgeFraction * headLength, each ring's normal blended from its
	//      two neighboring segments, rounding the corner into the "bulge" look some users prefer.
	//      Still meets the shaft with the shaft's own unblended radial normal and the head with the
	//      head's own unblended slant normal either way - only the interior optionally curves.
	//   3) head - a plain straight cone (headRadius -> 0 at the tip, linear profile, slant normal).
	// Absolute world units baked directly into the vertices (not normalized) - see this function's
	// only caller (renderSceneArrows) for why: any shaftWidth/headWidth/headLength/length/bulge-
	// strength edit rebuilds this arrow's mesh, but a position/orientation-only drag reuses it
	// unchanged through the draw transform.
	[[nodiscard]] RefinedConeMesh BuildWeldedArrowMesh(
		float shaftRadius,
		float headRadius,
		float headLength,
		float totalLength,
		std::uint32_t radialSegments,
		float bulgeStrength)
	{
		constexpr float TwoPi = 6.283185307f;
		radialSegments = std::max(radialSegments, 3u);
		const float shaftEnd = std::max(totalLength - headLength, 0.0f);

		RefinedConeMesh mesh;
		const std::size_t vertexBudget = static_cast<std::size_t>(radialSegments) * 11u + 1u;
		mesh.positions.reserve(vertexBudget);
		mesh.normals.reserve(vertexBudget);
		mesh.gradientT.reserve(vertexBudget);

		// normalRadial/normalZ are the SAME for every column of a ring - only cosAngle/sinAngle vary -
		// since every segment here is either a plain cylinder or a plain cone (constant slant along
		// its whole length), unlike the old blended-profile version this never needs to change from
		// ring to ring within one segment.
		auto emitRadialRing = [&](float z, float radius, float normalRadial, float normalZ) -> std::uint32_t {
			const std::uint32_t start = static_cast<std::uint32_t>(mesh.positions.size());
			for (std::uint32_t segment = 0; segment < radialSegments; ++segment)
			{
				const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(radialSegments);
				const float cosAngle = std::cos(angle);
				const float sinAngle = std::sin(angle);
				mesh.positions.emplace_back(cosAngle * radius, sinAngle * radius, z);
				mesh.normals.emplace_back(cosAngle * normalRadial, sinAngle * normalRadial, normalZ);
				mesh.gradientT.push_back(totalLength > 0.0001f ? z / totalLength : 0.0f);
			}
			return start;
		};
		auto emitFlatRing = [&](float z, float radius, float normalZ) -> std::uint32_t {
			const std::uint32_t start = static_cast<std::uint32_t>(mesh.positions.size());
			for (std::uint32_t segment = 0; segment < radialSegments; ++segment)
			{
				const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(radialSegments);
				mesh.positions.emplace_back(std::cos(angle) * radius, std::sin(angle) * radius, z);
				mesh.normals.emplace_back(0.0f, 0.0f, normalZ);
				mesh.gradientT.push_back(totalLength > 0.0001f ? z / totalLength : 0.0f);
			}
			return start;
		};
		auto connectRings = [&](std::uint32_t ringA, std::uint32_t ringB) {
			for (std::uint32_t segment = 0; segment < radialSegments; ++segment)
			{
				const std::uint32_t next = (segment + 1u) % radialSegments;
				const std::uint32_t bottom = ringA + segment;
				const std::uint32_t top = ringB + segment;
				const std::uint32_t nextBottom = ringA + next;
				const std::uint32_t nextTop = ringB + next;
				mesh.indices.push_back(bottom);
				mesh.indices.push_back(top);
				mesh.indices.push_back(nextTop);
				mesh.indices.push_back(bottom);
				mesh.indices.push_back(nextTop);
				mesh.indices.push_back(nextBottom);
			}
		};

		// Tail cap fan.
		const std::uint32_t tailCenter = static_cast<std::uint32_t>(mesh.positions.size());
		mesh.positions.emplace_back(0.0f, 0.0f, 0.0f);
		mesh.normals.emplace_back(0.0f, 0.0f, -1.0f);
		mesh.gradientT.push_back(0.0f);
		const std::uint32_t tailCapRing = emitFlatRing(0.0f, shaftRadius, -1.0f);
		for (std::uint32_t segment = 0; segment < radialSegments; ++segment)
		{
			const std::uint32_t next = (segment + 1u) % radialSegments;
			mesh.indices.push_back(tailCenter);
			mesh.indices.push_back(tailCapRing + next);
			mesh.indices.push_back(tailCapRing + segment);
		}

		// Shaft - plain cylinder, purely radial normal.
		const std::uint32_t shaftBottom = emitRadialRing(0.0f, shaftRadius, 1.0f, 0.0f);
		const std::uint32_t shaftTop = emitRadialRing(shaftEnd, shaftRadius, 1.0f, 0.0f);
		connectRings(shaftBottom, shaftTop);

		// Shoulder - the shaft/head transition. bulgeStrength <= 0 (the classic default) collapses this
		// to a flat annular disc at the shaft/head boundary, facing back toward the tail (-Z) when the
		// head is wider than the shaft (the normal case), forward otherwise - derived, not assumed, so
		// an unusual headWidth < shaftWidth arrow still shades correctly instead of showing an
		// inverted-normal patch. bulgeStrength > 0 spreads the same radius jump across a handful of
		// rings instead, each one's normal blended from its two neighboring edges in the (z, radius)
		// cross-section - a standard "vertex normal = average of adjacent face normals" approximation
		// of a curved profile, same idea as BuildRefinedSphereMesh's subdivision, just applied to a
		// revolved profile instead of a sphere.
		constexpr float kMaxBulgeFraction = 0.5f;
		const float bulgeZoneLength =
			std::clamp(bulgeStrength, 0.0f, 1.0f) * kMaxBulgeFraction * headLength;
		const float headBaseZ = shaftEnd + bulgeZoneLength;
		const float coneRunLength = std::max(totalLength - headBaseZ, 0.0001f);
		const glm::vec2 coneNormal2D =
			SafeNormalize(glm::vec2(coneRunLength, headRadius), glm::vec2(0.0f, 1.0f));

		std::uint32_t transitionEndRing = shaftTop;
		bool hasTransitionStrip = false;
		if (bulgeZoneLength <= 0.0001f)
		{
			const float shoulderNormalZ = (headRadius >= shaftRadius) ? -1.0f : 1.0f;
			const std::uint32_t shoulderInner = emitFlatRing(shaftEnd, shaftRadius, shoulderNormalZ);
			const std::uint32_t shoulderOuter = emitFlatRing(shaftEnd, headRadius, shoulderNormalZ);
			connectRings(shoulderInner, shoulderOuter);
		}
		else
		{
			constexpr std::uint32_t kInteriorRings = 4;
			constexpr std::uint32_t kSampleCount = kInteriorRings + 2u;
			std::array<float, kSampleCount> sampleZ{};
			std::array<float, kSampleCount> sampleR{};
			for (std::uint32_t i = 0; i < kSampleCount; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(kSampleCount - 1u);
				const float eased = t * t * (3.0f - 2.0f * t); // smoothstep
				sampleZ[i] = shaftEnd + bulgeZoneLength * t;
				sampleR[i] = shaftRadius + (headRadius - shaftRadius) * eased;
			}

			std::uint32_t previousRing = shaftTop;
			for (std::uint32_t i = 1; i < kSampleCount - 1u; ++i)
			{
				const glm::vec2 edgeIn = SafeNormalize(
					glm::vec2(sampleZ[i] - sampleZ[i - 1u], -(sampleR[i] - sampleR[i - 1u])), coneNormal2D);
				const glm::vec2 edgeOut = SafeNormalize(
					glm::vec2(sampleZ[i + 1u] - sampleZ[i], -(sampleR[i + 1u] - sampleR[i])), coneNormal2D);
				const glm::vec2 blended = SafeNormalize(edgeIn + edgeOut, coneNormal2D);
				const std::uint32_t ring = emitRadialRing(sampleZ[i], sampleR[i], blended.x, blended.y);
				connectRings(previousRing, ring);
				previousRing = ring;
			}
			transitionEndRing = previousRing;
			hasTransitionStrip = true;
		}

		// Head - plain straight cone down to a single tip point (radius 0), same slant-normal
		// construction as BuildRefinedConeMesh (verified algebraically against it when this function
		// was first written): tangent (dRadius, dZ) = (-headRadius, coneRunLength) rotates to
		// (coneRunLength, headRadius) - linear the whole way, no interior rings, so the silhouette
		// past the shoulder is always a plain triangle in profile.
		const std::uint32_t headBase = emitRadialRing(headBaseZ, headRadius, coneNormal2D.x, coneNormal2D.y);
		if (hasTransitionStrip)
			connectRings(transitionEndRing, headBase);
		const std::uint32_t tip = emitRadialRing(totalLength, 0.0f, coneNormal2D.x, coneNormal2D.y);
		connectRings(headBase, tip);

		return mesh;
	}

	// Uploads a freshly-built welded arrow mesh into `cache.mesh`'s GPU buffers - same
	// {position,normal,gradientT} vertex layout and instanced {model,colorA,colorB} attributes as
	// m_CylinderMesh/m_ConeMesh (see createCylinderMesh) so it draws through the same "bonds"
	// program, just GL_DYNAMIC_DRAW and rebuildable instead of a one-time static upload. VAO/buffers
	// are created once per cache slot (mesh.vao == 0 the first time) and reused on every later
	// rebuild via glBufferData - never deleted and recreated, so an interactive drag that changes
	// shaftWidth every frame doesn't churn GL object allocations.
	void UploadSceneArrowMesh(OpenGlMeshHandles &mesh, const RefinedConeMesh &welded)
	{
		std::vector<CylinderVertex> vertices(welded.positions.size());
		for (std::size_t index = 0; index < welded.positions.size(); ++index)
		{
			vertices[index].position = welded.positions[index];
			vertices[index].normal = SafeNormalize(welded.normals[index], glm::vec3(0.0f, 1.0f, 0.0f));
			vertices[index].gradientT = welded.gradientT[index];
		}

		const bool firstTime = mesh.vao == 0;
		if (firstTime)
		{
			glGenVertexArrays(1, &mesh.vao);
			glGenBuffers(1, &mesh.vbo);
			glGenBuffers(1, &mesh.ebo);
			glGenBuffers(1, &mesh.instanceVbo);
		}

		glBindVertexArray(mesh.vao);
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
		glBufferData(
			GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(CylinderVertex)), vertices.data(),
			GL_DYNAMIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
		glBufferData(
			GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(welded.indices.size() * sizeof(std::uint32_t)),
			welded.indices.data(), GL_DYNAMIC_DRAW);

		if (firstTime)
		{
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(
				0, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(
				1, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, normal)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(
				2, 1, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, gradientT)));

			glBindBuffer(GL_ARRAY_BUFFER, mesh.instanceVbo);
			glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(sizeof(OpenGlBondInstance)), nullptr, GL_DYNAMIC_DRAW);
			for (int column = 0; column < 4; ++column)
			{
				glEnableVertexAttribArray(3 + column);
				glVertexAttribPointer(
					3 + column, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance),
					reinterpret_cast<void *>(
						offsetof(OpenGlBondInstance, model) + sizeof(glm::vec4) * static_cast<std::size_t>(column)));
				glVertexAttribDivisor(3 + column, 1);
			}
			glEnableVertexAttribArray(7);
			glVertexAttribPointer(
				7, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance), reinterpret_cast<void *>(offsetof(OpenGlBondInstance, colorA)));
			glVertexAttribDivisor(7, 1);
			glEnableVertexAttribArray(8);
			glVertexAttribPointer(
				8, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance), reinterpret_cast<void *>(offsetof(OpenGlBondInstance, colorB)));
			glVertexAttribDivisor(8, 1);
		}

		glBindVertexArray(0);
		mesh.indexCount = static_cast<int>(welded.indices.size());
	}

	// Deletes one mesh's GL objects and zeroes the handles - shared by OpenGlSceneArrowMeshCache
	// slots dropped on shrink (renderSceneArrows) and by releaseStaticGeometry's final cleanup, same
	// 4-handle pattern releaseStaticGeometry already uses inline for the shared static meshes.
	void DeleteMeshHandles(OpenGlMeshHandles &mesh)
	{
		if (mesh.instanceVbo != 0)
		{
			glDeleteBuffers(1, &mesh.instanceVbo);
			mesh.instanceVbo = 0;
		}
		if (mesh.ebo != 0)
		{
			glDeleteBuffers(1, &mesh.ebo);
			mesh.ebo = 0;
		}
		if (mesh.vbo != 0)
		{
			glDeleteBuffers(1, &mesh.vbo);
			mesh.vbo = 0;
		}
		if (mesh.vao != 0)
		{
			glDeleteVertexArrays(1, &mesh.vao);
			mesh.vao = 0;
		}
		mesh.indexCount = 0;
	}

	OpenGlRendererBackend::~OpenGlRendererBackend()
	{
		Shutdown();
	}

	Result<void> OpenGlRendererBackend::Initialize(
		const Path &shaderDirectory,
		const RendererPrimitiveMeshAssets &primitiveMeshes)
	{
		if (m_Initialized)
			return {};

		m_ShaderDirectory = shaderDirectory;
		Result<void> atomsLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"atoms",
			m_ShaderDirectory / Path("atoms.vert"),
			m_ShaderDirectory / Path("atoms.frag"));
		if (!atomsLoaded.HasValue())
			return atomsLoaded.Error();

		Result<void> bondsLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"bonds",
			m_ShaderDirectory / Path("bonds.vert"),
			m_ShaderDirectory / Path("bonds.frag"));
		if (!bondsLoaded.HasValue())
			return bondsLoaded.Error();

		Result<void> linesLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"lines",
			m_ShaderDirectory / Path("lines.vert"),
			m_ShaderDirectory / Path("lines.frag"));
		if (!linesLoaded.HasValue())
			return linesLoaded.Error();

		Result<void> gridLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"grid",
			m_ShaderDirectory / Path("grid.vert"),
			m_ShaderDirectory / Path("grid.frag"));
		if (!gridLoaded.HasValue())
			return gridLoaded.Error();

		Result<void> isosurfaceLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"isosurface",
			m_ShaderDirectory / Path("isosurface.vert"),
			m_ShaderDirectory / Path("isosurface.frag"));
		if (!isosurfaceLoaded.HasValue())
			return isosurfaceLoaded.Error();

		Result<void> computeLoaded = m_ShaderLibrary.LoadComputeProgram(
			"bond_compute",
			m_ShaderDirectory / Path("bond_transform.comp"));
		if (!computeLoaded.HasValue())
			return computeLoaded.Error();

		Result<void> isosurfaceComputeLoaded = m_ShaderLibrary.LoadComputeProgram(
			"isosurface_compute",
			m_ShaderDirectory / Path("isosurface_march.comp"));
		if (!isosurfaceComputeLoaded.HasValue())
			return isosurfaceComputeLoaded.Error();

		Result<void> labelsLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"labels",
			m_ShaderDirectory / Path("labels.vert"),
			m_ShaderDirectory / Path("labels.frag"));
		if (!labelsLoaded.HasValue())
			return labelsLoaded.Error();

		// Same vertex stage as "labels" (billboard positioning, identical instance layout) paired
		// with a flat-color fragment shader instead of MSDF glyph sampling - draws LabelStyle
		// background quads (see AppendLabelBackgroundInstance) through the same m_LabelQuadMesh.
		Result<void> labelBackgroundLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"label_background",
			m_ShaderDirectory / Path("labels.vert"),
			m_ShaderDirectory / Path("label_background.frag"));
		if (!labelBackgroundLoaded.HasValue())
			return labelBackgroundLoaded.Error();

		// SceneArrow Arrow2D quad - its own fragment shader (a shaft-rect + head-triangle SDF union,
		// forked from label_background.frag rather than editing it in place - real labels have no
		// head triangle to draw), paired with its own vertex stage instead of labels.vert because the
		// basis (camera-facing plane vs. a fixed world plane) is resolved CPU-side per instance, see
		// OpenGlArrowQuadInstance/ComputeArrowQuadBasis.
		Result<void> arrowQuadLoaded = m_ShaderLibrary.LoadGraphicsProgram(
			"arrow_quad",
			m_ShaderDirectory / Path("arrow_quad.vert"),
			m_ShaderDirectory / Path("arrow_quad.frag"));
		if (!arrowQuadLoaded.HasValue())
			return arrowQuadLoaded.Error();

		Result<void> geometryResult = createStaticGeometry(primitiveMeshes);
		if (!geometryResult.HasValue())
		{
			releaseStaticGeometry();
			return geometryResult.Error();
		}

		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(OpenGlDebugCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
		const std::array<unsigned int, 2> filteredMessageIds = {131185u, 131218u};
		glDebugMessageControl(
			GL_DEBUG_SOURCE_API,
			GL_DEBUG_TYPE_PERFORMANCE,
			GL_DONT_CARE,
			static_cast<int>(filteredMessageIds.size()),
			filteredMessageIds.data(),
			GL_FALSE);

#if defined(TRACY_ENABLE)
		TracyGpuContext;
#endif

		m_Initialized = true;
		DS_LOG_INFO("OpenGL renderer backend initialized");
		return {};
	}

	void OpenGlRendererBackend::Shutdown()
	{
		if (!m_Initialized)
			return;

		releaseStaticGeometry();
		m_Viewports.clear();
		m_Initialized = false;
	}

	void OpenGlRendererBackend::ReloadShadersIfNeeded()
	{
		if (!m_Initialized)
			return;
		m_ShaderLibrary.ReloadModifiedPrograms();
	}

	void OpenGlRendererBackend::CollectProfilingData()
	{
		if (!m_Initialized)
			return;
#if defined(TRACY_ENABLE)
		TracyGpuCollect;
#endif
	}

	void OpenGlRendererBackend::MarkGridDirty()
	{
		for (auto &[windowKey, resources] : m_Viewports)
		{
			(void)windowKey;
			resources.gridDirty = true;
		}
	}

	unsigned int OpenGlRendererBackend::RenderWindow(
		const std::string &windowKey,
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		const RendererGlobalRenderSettings &globalSettings,
		int viewportWidth,
		int viewportHeight,
		bool showAtoms,
		bool showBonds,
		bool showCellBox,
		bool showGrid,
		bool showLabels,
		const std::vector<RendererWindowState::PinnedMeasurement> &pinnedMeasurements,
		const std::vector<std::size_t> &selectedPinnedMeasurements,
		const std::vector<RendererWindowState::FreeLabel> &freeLabels,
		const std::vector<std::size_t> &selectedFreeLabels,
		const std::vector<RendererWindowState::SceneArrow> &sceneArrows,
		const std::vector<std::size_t> &selectedSceneArrows,
		const std::vector<std::size_t> &selectedAtomIndices,
		const std::vector<std::size_t> &selectedBondIndices,
		const std::vector<IsosurfaceVertex> *debugIsosurfaceMesh,
		const RendererWindowState::OrbitalOverlayChannel *orbitalChannelUp,
		const RendererWindowState::OrbitalOverlayChannel *orbitalChannelDown,
		const glm::vec3 &sceneOffset)
	{
		if (!m_Initialized)
			return 0;

		OpenGlViewportResources &resources = viewportResources(windowKey, viewportWidth, viewportHeight);
		const std::string sourcePathKey = structure.sourcePath.String();
		if (resources.lastSourcePath != sourcePathKey)
		{
			resources.atomsDirty = true;
			resources.bondsDirty = true;
			resources.gridDirty = true;
			resources.cellEdgesDirty = true;
			resources.labelsDirty = true;
			resources.lastSourcePath = sourcePathKey;
		}
		if (resources.lastAtomCount != structure.atoms.size())
		{
			resources.atomsDirty = true;
			resources.lastAtomCount = structure.atoms.size();
		}
		if (resources.lastBondCount != structure.bonds.size())
		{
			resources.bondsDirty = true;
			resources.labelsDirty = true;
			resources.lastBondCount = structure.bonds.size();
		}
		if (resources.lastSelectedCount != selectedAtomIndices.size())
		{
			resources.atomsDirty = true;
			resources.lastSelectedCount = selectedAtomIndices.size();
		}
		std::size_t selectionHash = 1469598103934665603ull;
		for (const std::size_t index : selectedAtomIndices)
		{
			selectionHash ^= index + 0x9e3779b97f4a7c15ull + (selectionHash << 6) + (selectionHash >> 2);
		}
		if (resources.lastSelectionHash != selectionHash)
		{
			resources.atomsDirty = true;
			resources.lastSelectionHash = selectionHash;
		}
		if (resources.lastSelectedBondCount != selectedBondIndices.size())
		{
			resources.bondsDirty = true;
			resources.lastSelectedBondCount = selectedBondIndices.size();
		}
		std::size_t bondSelectionHash = 1469598103934665603ull;
		for (const std::size_t index : selectedBondIndices)
		{
			bondSelectionHash ^= index + 0x9e3779b97f4a7c15ull + (bondSelectionHash << 6) + (bondSelectionHash >> 2);
		}
		if (resources.lastBondSelectionHash != bondSelectionHash)
		{
			resources.bondsDirty = true;
			resources.lastBondSelectionHash = bondSelectionHash;
		}
		std::size_t bondVisibilityHash = 1469598103934665603ull;
		for (std::size_t index = 0; index < structure.bonds.size(); ++index)
		{
			if (!structure.bonds[index].visible)
				bondVisibilityHash ^= index + 0x9e3779b97f4a7c15ull + (bondVisibilityHash << 6) + (bondVisibilityHash >> 2);
		}
		if (resources.lastBondVisibilityHash != bondVisibilityHash)
		{
			resources.bondsDirty = true;
			resources.labelsDirty = true;
			resources.lastBondVisibilityHash = bondVisibilityHash;
		}
		std::size_t visibilityHash = 1469598103934665603ull;
		for (std::size_t index = 0; index < structure.atoms.size(); ++index)
		{
			if (!structure.atoms[index].visible)
				visibilityHash ^= index + 0x9e3779b97f4a7c15ull + (visibilityHash << 6) + (visibilityHash >> 2);
		}
		if (resources.lastVisibilityHash != visibilityHash)
		{
			resources.atomsDirty = true;
			resources.bondsDirty = true;
			resources.labelsDirty = true;
			resources.lastVisibilityHash = visibilityHash;
		}
		// Catches atom-position edits that don't touch count/selection/visibility - e.g. an
		// ImGuizmo drag (RendererPanel::renderTransformGizmo mutates cartesianPosition directly
		// every frame while dragging). Without this, atoms/bonds only redraw their last-uploaded
		// positions until some unrelated dirty condition above happens to fire.
		std::size_t positionHash = 1469598103934665603ull;
		for (const RendererAtomData &atom : structure.atoms)
		{
			for (int component = 0; component < 3; ++component)
			{
				std::uint32_t bits = 0;
				std::memcpy(&bits, &atom.cartesianPosition[component], sizeof(bits));
				positionHash ^= bits + 0x9e3779b97f4a7c15ull + (positionHash << 6) + (positionHash >> 2);
			}
		}
		if (resources.lastPositionHash != positionHash)
		{
			resources.atomsDirty = true;
			resources.bondsDirty = true;
			resources.labelsDirty = true;
			resources.lastPositionHash = positionHash;
		}
		// Catches atom color/radius edits (e.g. "Change atom type") that leave count/selection/
		// visibility/position untouched - see lastColorHash's declaration comment.
		std::size_t colorHash = 1469598103934665603ull;
		for (const RendererAtomData &atom : structure.atoms)
		{
			std::uint32_t bits = 0;
			std::memcpy(&bits, &atom.radius, sizeof(bits));
			colorHash ^= bits + 0x9e3779b97f4a7c15ull + (colorHash << 6) + (colorHash >> 2);
			for (int component = 0; component < 3; ++component)
			{
				std::memcpy(&bits, &atom.color[component], sizeof(bits));
				colorHash ^= bits + 0x9e3779b97f4a7c15ull + (colorHash << 6) + (colorHash >> 2);
			}
		}
		if (resources.lastColorHash != colorHash)
		{
			resources.atomsDirty = true;
			resources.bondsDirty = true;
			resources.lastColorHash = colorHash;
		}
		if (resources.lastBondRadiusMultiplier != globalSettings.bondRadiusMultiplier)
		{
			resources.bondsDirty = true;
			resources.lastBondRadiusMultiplier = globalSettings.bondRadiusMultiplier;
		}
		resources.frameBuffer.Bind();
		glViewport(0, 0, resources.frameBuffer.Width(), resources.frameBuffer.Height());
		glClearColor(
			globalSettings.backgroundColor.r,
			globalSettings.backgroundColor.g,
			globalSettings.backgroundColor.b,
			globalSettings.backgroundColor.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		configureOpenGlState();

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Pass");
#endif

		if (showGrid)
			renderGrid(structure, camera, resources, globalSettings, sceneOffset);
		if (showCellBox)
			renderCellBox(structure, camera, resources, sceneOffset);
		if (showBonds)
			renderBonds(structure, camera, resources, globalSettings, selectedBondIndices, sceneOffset);
		const glm::vec2 viewportPixelSize(
			static_cast<float>(resources.frameBuffer.Width()), static_cast<float>(resources.frameBuffer.Height()));
		if (!sceneArrows.empty())
			renderSceneArrows(
				sceneArrows, selectedSceneArrows, camera, resources, globalSettings, false, viewportPixelSize,
				sceneOffset);
		if (showAtoms)
			renderAtoms(structure, camera, resources, globalSettings, selectedAtomIndices, sceneOffset);
		if (debugIsosurfaceMesh && !debugIsosurfaceMesh->empty())
			renderIsosurfaceOverlay(*debugIsosurfaceMesh, camera, globalSettings);
		if (orbitalChannelUp != nullptr && orbitalChannelUp->enabled && orbitalChannelUp->vertexCount > 0)
			renderIsosurfaceGpuOverlay(resources.isosurfaceVao[0], orbitalChannelUp->vertexCount, camera, globalSettings,
				orbitalChannelUp->positiveLobeColor, orbitalChannelUp->negativeLobeColor, orbitalChannelUp->lobeAlpha,
				sceneOffset);
		if (orbitalChannelDown != nullptr && orbitalChannelDown->enabled && orbitalChannelDown->vertexCount > 0)
			renderIsosurfaceGpuOverlay(resources.isosurfaceVao[1], orbitalChannelDown->vertexCount, camera, globalSettings,
				orbitalChannelDown->positiveLobeColor, orbitalChannelDown->negativeLobeColor,
				orbitalChannelDown->lobeAlpha, sceneOffset);
		// Drawn last and depth-test-disabled (see renderLabels) so an annotation always reads clearly
		// on top of the structure, regardless of where in 3D space it's anchored.
		if (showLabels || !pinnedMeasurements.empty() || !freeLabels.empty())
		{
			renderLabels(
				structure, camera, resources, showLabels, pinnedMeasurements, selectedPinnedMeasurements,
				freeLabels, selectedFreeLabels, sceneOffset);
		}
		// Arrow2D's own late, depth-disabled pass (see renderSceneArrows's declaration comment) -
		// same "annotation always reads on top" reasoning as renderLabels just above.
		if (!sceneArrows.empty())
			renderSceneArrows(
				sceneArrows, selectedSceneArrows, camera, resources, globalSettings, true, viewportPixelSize,
				sceneOffset);

		resources.frameBuffer.Unbind();
		resources.lastRenderTime = Time::NowSteady();
		return resources.frameBuffer.ColorTextureId();
	}

	Result<void> OpenGlRendererBackend::createStaticGeometry(const RendererPrimitiveMeshAssets &primitiveMeshes)
	{
		Result<void> sphereResult = createSphereMesh(primitiveMeshes.sphere);
		if (!sphereResult.HasValue())
			return sphereResult.Error();

		Result<void> cylinderResult = createCylinderMesh(primitiveMeshes.cylinder);
		if (!cylinderResult.HasValue())
			return cylinderResult.Error();

		Result<void> coneResult = createConeMesh(primitiveMeshes.cone);
		if (!coneResult.HasValue())
			return coneResult.Error();

		createScreenGrid();
		createIsosurfaceGeometry();
		createLabelQuadMesh();
		createArrowQuadMesh();
		glGenBuffers(1, &m_ComputeInputSsbo);
		glGenBuffers(1, &m_ComputeOutputSsbo);
		return {};
	}

	void OpenGlRendererBackend::releaseStaticGeometry()
	{
		if (m_ComputeOutputSsbo != 0)
		{
			glDeleteBuffers(1, &m_ComputeOutputSsbo);
			m_ComputeOutputSsbo = 0;
		}
		if (m_ComputeInputSsbo != 0)
		{
			glDeleteBuffers(1, &m_ComputeInputSsbo);
			m_ComputeInputSsbo = 0;
		}

		if (m_LineVbo != 0)
		{
			glDeleteBuffers(1, &m_LineVbo);
			m_LineVbo = 0;
		}
		if (m_LineVao != 0)
		{
			glDeleteVertexArrays(1, &m_LineVao);
			m_LineVao = 0;
		}
		if (m_IsosurfaceVbo != 0)
		{
			glDeleteBuffers(1, &m_IsosurfaceVbo);
			m_IsosurfaceVbo = 0;
		}
		if (m_IsosurfaceVao != 0)
		{
			glDeleteVertexArrays(1, &m_IsosurfaceVao);
			m_IsosurfaceVao = 0;
		}
		if (m_IsosurfaceGridSsbo != 0)
		{
			glDeleteBuffers(1, &m_IsosurfaceGridSsbo);
			m_IsosurfaceGridSsbo = 0;
		}
		for (auto &[windowKey, resources] : m_Viewports)
		{
			(void)windowKey;
			for (int slot = 0; slot < kIsosurfaceSlotCount; ++slot)
			{
				if (resources.isosurfaceVertexSsbo[slot] != 0)
				{
					glDeleteBuffers(1, &resources.isosurfaceVertexSsbo[slot]);
					resources.isosurfaceVertexSsbo[slot] = 0;
				}
				if (resources.isosurfaceCounterSsbo[slot] != 0)
				{
					glDeleteBuffers(1, &resources.isosurfaceCounterSsbo[slot]);
					resources.isosurfaceCounterSsbo[slot] = 0;
				}
				if (resources.isosurfaceVao[slot] != 0)
				{
					glDeleteVertexArrays(1, &resources.isosurfaceVao[slot]);
					resources.isosurfaceVao[slot] = 0;
				}
			}
			for (OpenGlSceneArrowMeshCache &cacheEntry : resources.sceneArrow3DMeshCache)
				DeleteMeshHandles(cacheEntry.mesh);
			resources.sceneArrow3DMeshCache.clear();
		}

		m_LabelFont.reset();

		const std::array<OpenGlMeshHandles *, 5> meshes = {
			&m_SphereMesh, &m_CylinderMesh, &m_ConeMesh, &m_LabelQuadMesh, &m_ArrowQuadMesh};
		for (OpenGlMeshHandles *mesh : meshes)
		{
			if (mesh->instanceVbo != 0)
			{
				glDeleteBuffers(1, &mesh->instanceVbo);
				mesh->instanceVbo = 0;
			}
			if (mesh->ebo != 0)
			{
				glDeleteBuffers(1, &mesh->ebo);
				mesh->ebo = 0;
			}
			if (mesh->vbo != 0)
			{
				glDeleteBuffers(1, &mesh->vbo);
				mesh->vbo = 0;
			}
			if (mesh->vao != 0)
			{
				glDeleteVertexArrays(1, &mesh->vao);
				mesh->vao = 0;
			}
			mesh->indexCount = 0;
		}
	}

	Result<void> OpenGlRendererBackend::createSphereMesh(const RendererStaticMeshData &meshData)
	{
		if (meshData.positions.empty() || meshData.indices.empty())
		{
			return MakeMeshAssetError(
				"renderer.mesh.sphere.empty",
				"Sphere mesh asset is empty.",
				"Sphere mesh positions/indices are empty.");
		}

		std::string indexError;
		if (!ValidateIndexRange(meshData.indices, meshData.positions.size(), indexError))
		{
			return MakeMeshAssetError(
				"renderer.mesh.sphere.indices_out_of_range",
				"Sphere mesh asset is invalid.",
				"Sphere mesh has invalid indices: " + indexError);
		}
		if (meshData.indices.size() % 3u != 0u)
		{
			return MakeMeshAssetError(
				"renderer.mesh.sphere.non_triangular",
				"Sphere mesh asset is invalid.",
				"Sphere mesh index count is not divisible by 3.");
		}

		const RefinedSphereMesh refinedMesh = BuildRefinedSphereMesh(meshData);

		std::vector<SphereVertex> vertices;
		vertices.resize(refinedMesh.positions.size());
		for (std::size_t index = 0; index < refinedMesh.positions.size(); ++index)
		{
			vertices[index].position = refinedMesh.positions[index];
			vertices[index].normal = SafeNormalize(refinedMesh.positions[index], glm::vec3(0.0f, 1.0f, 0.0f));
		}

		glGenVertexArrays(1, &m_SphereMesh.vao);
		glGenBuffers(1, &m_SphereMesh.vbo);
		glGenBuffers(1, &m_SphereMesh.ebo);
		glGenBuffers(1, &m_SphereMesh.instanceVbo);
		glBindVertexArray(m_SphereMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_SphereMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(SphereVertex)), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SphereMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(refinedMesh.indices.size() * sizeof(std::uint32_t)), refinedMesh.indices.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, normal)));

		glBindBuffer(GL_ARRAY_BUFFER, m_SphereMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlAtomInstance), reinterpret_cast<void *>(offsetof(OpenGlAtomInstance, positionRadius)));
		glVertexAttribDivisor(2, 1);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlAtomInstance), reinterpret_cast<void *>(offsetof(OpenGlAtomInstance, color)));
		glVertexAttribDivisor(3, 1);

		glBindVertexArray(0);
		m_SphereMesh.indexCount = static_cast<int>(refinedMesh.indices.size());
		return {};
	}

	Result<void> OpenGlRendererBackend::createCylinderMesh(const RendererStaticMeshData &meshData)
	{
		if (meshData.positions.empty() || meshData.indices.empty())
		{
			return MakeMeshAssetError(
				"renderer.mesh.cylinder.empty",
				"Cylinder mesh asset is empty.",
				"Cylinder mesh positions/indices are empty.");
		}

		std::string indexError;
		if (!ValidateIndexRange(meshData.indices, meshData.positions.size(), indexError))
		{
			return MakeMeshAssetError(
				"renderer.mesh.cylinder.indices_out_of_range",
				"Cylinder mesh asset is invalid.",
				"Cylinder mesh has invalid indices: " + indexError);
		}

		const RefinedCylinderMesh refinedMesh = BuildRefinedCylinderMesh(meshData);

		std::vector<CylinderVertex> vertices;
		vertices.resize(refinedMesh.positions.size());
		for (std::size_t index = 0; index < refinedMesh.positions.size(); ++index)
		{
			vertices[index].position = refinedMesh.positions[index];
			vertices[index].normal = SafeNormalize(refinedMesh.normals[index], glm::vec3(0.0f, 1.0f, 0.0f));
			vertices[index].gradientT = refinedMesh.gradientT[index];
		}

		glGenVertexArrays(1, &m_CylinderMesh.vao);
		glGenBuffers(1, &m_CylinderMesh.vbo);
		glGenBuffers(1, &m_CylinderMesh.ebo);
		glGenBuffers(1, &m_CylinderMesh.instanceVbo);
		glBindVertexArray(m_CylinderMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(CylinderVertex)), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_CylinderMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(refinedMesh.indices.size() * sizeof(std::uint32_t)), refinedMesh.indices.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, normal)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, gradientT)));

		glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		for (int column = 0; column < 4; ++column)
		{
			glEnableVertexAttribArray(3 + column);
			glVertexAttribPointer(
				3 + column,
				4,
				GL_FLOAT,
				GL_FALSE,
				sizeof(OpenGlBondInstance),
				reinterpret_cast<void *>(offsetof(OpenGlBondInstance, model) + sizeof(glm::vec4) * static_cast<std::size_t>(column)));
			glVertexAttribDivisor(3 + column, 1);
		}
		glEnableVertexAttribArray(7);
		glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance), reinterpret_cast<void *>(offsetof(OpenGlBondInstance, colorA)));
		glVertexAttribDivisor(7, 1);
		glEnableVertexAttribArray(8);
		glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance), reinterpret_cast<void *>(offsetof(OpenGlBondInstance, colorB)));
		glVertexAttribDivisor(8, 1);

		glBindVertexArray(0);
		m_CylinderMesh.indexCount = static_cast<int>(refinedMesh.indices.size());
		return {};
	}

	Result<void> OpenGlRendererBackend::createConeMesh(const RendererStaticMeshData &meshData)
	{
		if (meshData.positions.empty() || meshData.indices.empty())
		{
			return MakeMeshAssetError(
				"renderer.mesh.cone.empty",
				"Cone mesh asset is empty.",
				"Cone mesh positions/indices are empty.");
		}

		std::string indexError;
		if (!ValidateIndexRange(meshData.indices, meshData.positions.size(), indexError))
		{
			return MakeMeshAssetError(
				"renderer.mesh.cone.indices_out_of_range",
				"Cone mesh asset is invalid.",
				"Cone mesh has invalid indices: " + indexError);
		}

		// Capless - m_ConeMesh is only ever used for SceneArrow's Arrow3D head (see
		// BuildRefinedConeMesh's declaration comment for why a flat base cap was removed).
		const RefinedConeMesh refinedMesh = BuildRefinedConeMesh(meshData, false);

		// Reuses CylinderVertex - same {position, normal, gradientT} layout, and the cone head is
		// drawn through the same "bonds" program/OpenGlBondInstance as the shaft (see
		// renderSceneArrows), so the vertex format has to match exactly.
		std::vector<CylinderVertex> vertices;
		vertices.resize(refinedMesh.positions.size());
		for (std::size_t index = 0; index < refinedMesh.positions.size(); ++index)
		{
			vertices[index].position = refinedMesh.positions[index];
			vertices[index].normal = SafeNormalize(refinedMesh.normals[index], glm::vec3(0.0f, 1.0f, 0.0f));
			vertices[index].gradientT = refinedMesh.gradientT[index];
		}

		glGenVertexArrays(1, &m_ConeMesh.vao);
		glGenBuffers(1, &m_ConeMesh.vbo);
		glGenBuffers(1, &m_ConeMesh.ebo);
		glGenBuffers(1, &m_ConeMesh.instanceVbo);
		glBindVertexArray(m_ConeMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_ConeMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(CylinderVertex)), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ConeMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(refinedMesh.indices.size() * sizeof(std::uint32_t)), refinedMesh.indices.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, normal)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(CylinderVertex), reinterpret_cast<void *>(offsetof(CylinderVertex, gradientT)));

		glBindBuffer(GL_ARRAY_BUFFER, m_ConeMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		for (int column = 0; column < 4; ++column)
		{
			glEnableVertexAttribArray(3 + column);
			glVertexAttribPointer(
				3 + column,
				4,
				GL_FLOAT,
				GL_FALSE,
				sizeof(OpenGlBondInstance),
				reinterpret_cast<void *>(offsetof(OpenGlBondInstance, model) + sizeof(glm::vec4) * static_cast<std::size_t>(column)));
			glVertexAttribDivisor(3 + column, 1);
		}
		glEnableVertexAttribArray(7);
		glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance), reinterpret_cast<void *>(offsetof(OpenGlBondInstance, colorA)));
		glVertexAttribDivisor(7, 1);
		glEnableVertexAttribArray(8);
		glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlBondInstance), reinterpret_cast<void *>(offsetof(OpenGlBondInstance, colorB)));
		glVertexAttribDivisor(8, 1);

		glBindVertexArray(0);
		m_ConeMesh.indexCount = static_cast<int>(refinedMesh.indices.size());
		return {};
	}

	constexpr glm::vec2 kQuadVertices[4] = {
		glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f)};
	constexpr std::uint32_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

	void OpenGlRendererBackend::createLabelQuadMesh()
	{
		glGenVertexArrays(1, &m_LabelQuadMesh.vao);
		glGenBuffers(1, &m_LabelQuadMesh.vbo);
		glGenBuffers(1, &m_LabelQuadMesh.ebo);
		glGenBuffers(1, &m_LabelQuadMesh.instanceVbo);
		glBindVertexArray(m_LabelQuadMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_LabelQuadMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_LabelQuadMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, m_LabelQuadMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, worldCenter)));
		glVertexAttribDivisor(1, 1);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, localOffsetSize)));
		glVertexAttribDivisor(2, 1);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, atlasUvMinMax)));
		glVertexAttribDivisor(3, 1);
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, color)));
		glVertexAttribDivisor(4, 1);
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, rotationRadians)));
		glVertexAttribDivisor(5, 1);
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, outlineColor)));
		glVertexAttribDivisor(6, 1);
		glEnableVertexAttribArray(7);
		glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, outlineWidth)));
		glVertexAttribDivisor(7, 1);
		glEnableVertexAttribArray(8);
		glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, cornerRadius)));
		glVertexAttribDivisor(8, 1);
		glEnableVertexAttribArray(9);
		glVertexAttribPointer(9, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, strokeColor)));
		glVertexAttribDivisor(9, 1);
		glEnableVertexAttribArray(10);
		glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlLabelInstance),
			reinterpret_cast<void *>(offsetof(OpenGlLabelInstance, strokeWidth)));
		glVertexAttribDivisor(10, 1);

		glBindVertexArray(0);
		m_LabelQuadMesh.indexCount = 6;
	}

	// SceneArrow Arrow2D quad - shares the same static kQuadVertices/kQuadIndices geometry as
	// m_LabelQuadMesh above, but OpenGlArrowQuadInstance is a different layout than
	// OpenGlLabelInstance (world-space right/up basis instead of a camera-derived billboard), so it
	// needs its own VAO/instance VBO rather than reusing m_LabelQuadMesh's.
	void OpenGlRendererBackend::createArrowQuadMesh()
	{
		glGenVertexArrays(1, &m_ArrowQuadMesh.vao);
		glGenBuffers(1, &m_ArrowQuadMesh.vbo);
		glGenBuffers(1, &m_ArrowQuadMesh.ebo);
		glGenBuffers(1, &m_ArrowQuadMesh.instanceVbo);
		glBindVertexArray(m_ArrowQuadMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_ArrowQuadMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ArrowQuadMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, m_ArrowQuadMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, worldCenter)));
		glVertexAttribDivisor(1, 1);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, right)));
		glVertexAttribDivisor(2, 1);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, up)));
		glVertexAttribDivisor(3, 1);
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, halfSize)));
		glVertexAttribDivisor(4, 1);
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, color)));
		glVertexAttribDivisor(5, 1);
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, outlineColor)));
		glVertexAttribDivisor(6, 1);
		glEnableVertexAttribArray(7);
		glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, outlineWidth)));
		glVertexAttribDivisor(7, 1);
		glEnableVertexAttribArray(8);
		glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, headHalfWidth)));
		glVertexAttribDivisor(8, 1);
		glEnableVertexAttribArray(9);
		glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, sizeof(OpenGlArrowQuadInstance),
			reinterpret_cast<void *>(offsetof(OpenGlArrowQuadInstance, headLength)));
		glVertexAttribDivisor(9, 1);

		glBindVertexArray(0);
		m_ArrowQuadMesh.indexCount = 6;
	}

	void OpenGlRendererBackend::createScreenGrid()
	{
		glGenVertexArrays(1, &m_LineVao);
		glGenBuffers(1, &m_LineVbo);
		glBindVertexArray(m_LineVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void *>(0));
		glBindVertexArray(0);
	}

	namespace
	{
		// 32 bytes/vertex * 3 * kMaxIsosurfaceGpuVertices ~= 192MB worst case, but real usage is
		// far smaller (marching-tetrahedra output scales with surface area, not grid volume) - the
		// singlet_HSE band-0 reference case used ~113k vertices. Overflow is dropped safely by the
		// compute shader's own bounds check, not corrupted.
		constexpr std::size_t kMaxIsosurfaceGpuVertices = 2'000'000;
		struct IsosurfaceGpuVertex
		{
			glm::vec4 position;
			glm::vec4 normalSign;
		};
	} // namespace

	void OpenGlRendererBackend::createIsosurfaceGeometry()
	{
		glGenVertexArrays(1, &m_IsosurfaceVao);
		glGenBuffers(1, &m_IsosurfaceVbo);
		glBindVertexArray(m_IsosurfaceVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_IsosurfaceVbo);
		glBufferData(GL_ARRAY_BUFFER, 1, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
			reinterpret_cast<void *>(offsetof(IsosurfaceVertex, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
			reinterpret_cast<void *>(offsetof(IsosurfaceVertex, normal)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
			reinterpret_cast<void *>(offsetof(IsosurfaceVertex, sign)));
		glBindVertexArray(0);

		glGenBuffers(1, &m_IsosurfaceGridSsbo);
	}

	void OpenGlRendererBackend::ensureIsosurfaceBuffers(OpenGlViewportResources &resources)
	{
		if (resources.isosurfaceVao[0] != 0)
			return;

		for (int slot = 0; slot < kIsosurfaceSlotCount; ++slot)
		{
			glGenBuffers(1, &resources.isosurfaceVertexSsbo[slot]);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.isosurfaceVertexSsbo[slot]);
			glBufferData(GL_SHADER_STORAGE_BUFFER,
				static_cast<GLsizeiptr>(kMaxIsosurfaceGpuVertices * sizeof(IsosurfaceGpuVertex)),
				nullptr, GL_DYNAMIC_COPY);
			glGenBuffers(1, &resources.isosurfaceCounterSsbo[slot]);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.isosurfaceCounterSsbo[slot]);
			glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_COPY);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

			// Same buffer, two roles: written as an SSBO by the compute shader, read as a vertex
			// buffer here - geometry never leaves the GPU between the two.
			glGenVertexArrays(1, &resources.isosurfaceVao[slot]);
			glBindVertexArray(resources.isosurfaceVao[slot]);
			glBindBuffer(GL_ARRAY_BUFFER, resources.isosurfaceVertexSsbo[slot]);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceGpuVertex),
				reinterpret_cast<void *>(offsetof(IsosurfaceGpuVertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceGpuVertex),
				reinterpret_cast<void *>(offsetof(IsosurfaceGpuVertex, normalSign)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceGpuVertex),
				reinterpret_cast<void *>(offsetof(IsosurfaceGpuVertex, normalSign) + sizeof(glm::vec3)));
			glBindVertexArray(0);
		}
	}

	void OpenGlRendererBackend::configureOpenGlState() const
	{
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}

	void OpenGlRendererBackend::renderAtoms(
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const std::vector<std::size_t> &selectedIndices,
		const glm::vec3 &sceneOffset)
	{
		if (resources.atomsDirty)
		{
			resources.cachedAtomInstances.clear();
			resources.cachedAtomInstances.reserve(structure.atoms.size());
			for (std::size_t i = 0; i < structure.atoms.size(); ++i)
			{
				const RendererAtomData &atom = structure.atoms[i];
				if (!atom.visible)
					continue;
				OpenGlAtomInstance instance;
				const bool isSelected = std::find(
					selectedIndices.begin(),
					selectedIndices.end(),
					i) != selectedIndices.end();
				instance.positionRadius = glm::vec4(atom.cartesianPosition, atom.radius);
				// Blender's selection orange (~#E8850C), blended rather than added so the
				// element's own color is still legible on the highlighted atom.
				constexpr glm::vec3 kSelectionHighlightColor(0.91f, 0.52f, 0.02f);
				const glm::vec3 displayColor = isSelected
					? glm::mix(atom.color, kSelectionHighlightColor, 0.55f)
					: atom.color;
				instance.color = glm::vec4(displayColor, 1.0f);
				resources.cachedAtomInstances.push_back(instance);
			}
		}
		if (resources.cachedAtomInstances.empty())
			return;

		const unsigned int program = m_ShaderLibrary.Program("atoms");
		if (program == 0)
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Atoms");
#endif

		glBindBuffer(GL_ARRAY_BUFFER, m_SphereMesh.instanceVbo);
		const GLsizeiptr requiredBytes = static_cast<GLsizeiptr>(
			resources.cachedAtomInstances.size() * sizeof(OpenGlAtomInstance));
		GLint currentSize = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
		if (static_cast<GLsizeiptr>(currentSize) < requiredBytes)
		{
			const GLsizeiptr newSize = requiredBytes + requiredBytes / 2;
			glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
		}
		glBufferSubData(
			GL_ARRAY_BUFFER,
			0,
			requiredBytes,
			resources.cachedAtomInstances.data());

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("atoms", "u_ViewProjection");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		const int keyDirectionLocation = m_ShaderLibrary.Uniform("atoms", "u_KeyDirection");
		const int fillDirectionLocation = m_ShaderLibrary.Uniform("atoms", "u_FillDirection");
		const int backDirectionLocation = m_ShaderLibrary.Uniform("atoms", "u_BackDirection");
		const int ambientLocation = m_ShaderLibrary.Uniform("atoms", "u_AmbientIntensity");
		const int keyIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_KeyIntensity");
		const int fillIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_FillIntensity");
		const int backIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_BackIntensity");
		const int twoSidedLocation = m_ShaderLibrary.Uniform("atoms", "u_TwoSidedLighting");
		const int cameraPositionLocation = m_ShaderLibrary.Uniform("atoms", "u_CameraPosition");
		const int specularIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_SpecularIntensity");
		const int shininessLocation = m_ShaderLibrary.Uniform("atoms", "u_Shininess");
		const int saturationLocation = m_ShaderLibrary.Uniform("atoms", "u_Saturation");
		const int sceneOffsetLocation = m_ShaderLibrary.Uniform("atoms", "u_SceneOffset");
		if (keyDirectionLocation >= 0)
			glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
		if (fillDirectionLocation >= 0)
			glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
		if (backDirectionLocation >= 0)
			glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
		if (ambientLocation >= 0)
			glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
		if (keyIntensityLocation >= 0)
			glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
		if (fillIntensityLocation >= 0)
			glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
		if (backIntensityLocation >= 0)
			glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
		if (twoSidedLocation >= 0)
			glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
		if (cameraPositionLocation >= 0)
		{
			const glm::vec3 cameraPosition = camera.Position();
			glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
		}
		if (specularIntensityLocation >= 0)
			glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
		if (shininessLocation >= 0)
			glUniform1f(shininessLocation, globalSettings.lighting.shininess);
		if (saturationLocation >= 0)
			glUniform1f(saturationLocation, globalSettings.colorSaturation);
		if (sceneOffsetLocation >= 0)
			glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

		glBindVertexArray(m_SphereMesh.vao);
		glDrawElementsInstanced(
			GL_TRIANGLES,
			m_SphereMesh.indexCount,
			GL_UNSIGNED_INT,
			nullptr,
			static_cast<int>(resources.cachedAtomInstances.size()));
		glBindVertexArray(0);
		resources.atomsDirty = false;
	}

	void OpenGlRendererBackend::renderBonds(
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const std::vector<std::size_t> &selectedIndices,
		const glm::vec3 &sceneOffset)
	{
		if (resources.bondsDirty)
		{
			resources.cachedBondInstances.clear();
			resources.cachedBondInstances.reserve(structure.bonds.size());
			for (std::size_t bondIndex = 0; bondIndex < structure.bonds.size(); ++bondIndex)
			{
				const RendererBondData &bond = structure.bonds[bondIndex];
				if (!bond.visible)
					continue;
				if (bond.firstAtomIndex >= structure.atoms.size() || bond.secondAtomIndex >= structure.atoms.size())
					continue;
				const RendererAtomData &firstAtom = structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &secondAtom = structure.atoms[bond.secondAtomIndex];
				if (!firstAtom.visible || !secondAtom.visible)
					continue;
				// secondAtomPosition, not secondAtom.cartesianPosition directly: nonzero for bonds
				// that cross a periodic cell boundary (see RendererBondData::secondAtomPeriodicOffset)
				// - the bond then correctly extends a little past the cell edge instead of drawing
				// a bogus line straight across the whole cell to the atom's real (unwrapped) position.
				const glm::vec3 secondAtomPosition = secondAtom.cartesianPosition + bond.secondAtomPeriodicOffset;
				const glm::vec3 axis = secondAtomPosition - firstAtom.cartesianPosition;
				const float axisLength = glm::length(axis);
				if (!std::isfinite(axisLength) || axisLength <= 0.0001f)
					continue;
				// bondRadius (unmultiplied) still feeds buildBondTransform below, matching bonds.vert's
				// own "model matrix bakes bond.radius, shader multiplies by u_BondRadiusMultiplier on
				// top" split (see bonds.vert:16-19) - a live thickness knob without re-baking cached
				// instance data. The shrink/trim math, however, must use the *rendered* radius
				// (trimRadius) since that's the cylinder surface actually visible on screen -
				// otherwise the trim point diverges from it whenever the multiplier != 1, showing a
				// gap (multiplier < 1) or overlap (> 1) at the atom junction. See
				// lastBondRadiusMultiplier in OpenGlViewportResources for the cache invalidation this
				// depends on.
				const float bondRadius = std::max(bond.radius, 0.001f);
				const float trimRadius = bondRadius * globalSettings.bondRadiusMultiplier;
				const glm::vec3 direction = axis / axisLength;
				const float rawShrinkA = std::sqrt(
					std::max(firstAtom.radius * firstAtom.radius - trimRadius * trimRadius, 0.0f));
				const float shrinkA = std::min(rawShrinkA, axisLength * 0.45f);
				const float rawShrinkB = std::sqrt(
					std::max(secondAtom.radius * secondAtom.radius - trimRadius * trimRadius, 0.0f));
				const float shrinkB = std::min(rawShrinkB, axisLength * 0.45f);
				const float trimmedLength = axisLength - shrinkA - shrinkB;
				if (trimmedLength <= 0.001f)
					continue;
				const glm::vec3 bondStart = firstAtom.cartesianPosition + direction * shrinkA;
				const glm::vec3 bondEnd = secondAtomPosition - direction * shrinkB;
				const bool isSelected = std::find(
					selectedIndices.begin(), selectedIndices.end(), bondIndex) != selectedIndices.end();
				// Same Blender selection orange as renderAtoms, blended the same way.
				constexpr glm::vec3 kSelectionHighlightColor(0.91f, 0.52f, 0.02f);
				OpenGlBondInstance instance;
				instance.model = buildBondTransform(bondStart, bondEnd, bondRadius);
				instance.colorA = glm::vec4(
					isSelected ? glm::mix(bond.gradient.start, kSelectionHighlightColor, 0.55f) : bond.gradient.start, 1.0f);
				instance.colorB = glm::vec4(
					isSelected ? glm::mix(bond.gradient.finish, kSelectionHighlightColor, 0.55f) : bond.gradient.finish, 1.0f);
				resources.cachedBondInstances.push_back(instance);
			}
		}
		if (resources.cachedBondInstances.empty())
			return;

		const unsigned int program = m_ShaderLibrary.Program("bonds");
		if (program == 0)
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Bonds");
#endif

		glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.instanceVbo);
		const GLsizeiptr requiredBytes = static_cast<GLsizeiptr>(
			resources.cachedBondInstances.size() * sizeof(OpenGlBondInstance));
		GLint currentSize = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
		if (static_cast<GLsizeiptr>(currentSize) < requiredBytes)
		{
			const GLsizeiptr newSize = requiredBytes + requiredBytes / 2;
			glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
		}
		glBufferSubData(
			GL_ARRAY_BUFFER,
			0,
			requiredBytes,
			resources.cachedBondInstances.data());

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("bonds", "u_ViewProjection");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		const int keyDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_KeyDirection");
		const int fillDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_FillDirection");
		const int backDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_BackDirection");
		const int ambientLocation = m_ShaderLibrary.Uniform("bonds", "u_AmbientIntensity");
		const int keyIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_KeyIntensity");
		const int fillIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_FillIntensity");
		const int backIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_BackIntensity");
		const int twoSidedLocation = m_ShaderLibrary.Uniform("bonds", "u_TwoSidedLighting");
		const int cameraPositionLocation = m_ShaderLibrary.Uniform("bonds", "u_CameraPosition");
		const int specularIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_SpecularIntensity");
		const int shininessLocation = m_ShaderLibrary.Uniform("bonds", "u_Shininess");
		const int saturationLocation = m_ShaderLibrary.Uniform("bonds", "u_Saturation");
		const int specularScaleLocation = m_ShaderLibrary.Uniform("bonds", "u_SpecularScale");
		const int bondRadiusMultiplierLocation = m_ShaderLibrary.Uniform("bonds", "u_BondRadiusMultiplier");
		const int sceneOffsetLocation = m_ShaderLibrary.Uniform("bonds", "u_SceneOffset");
		if (keyDirectionLocation >= 0)
			glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
		if (fillDirectionLocation >= 0)
			glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
		if (backDirectionLocation >= 0)
			glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
		if (ambientLocation >= 0)
			glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
		if (keyIntensityLocation >= 0)
			glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
		if (fillIntensityLocation >= 0)
			glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
		if (backIntensityLocation >= 0)
			glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
		if (twoSidedLocation >= 0)
			glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
		if (cameraPositionLocation >= 0)
		{
			const glm::vec3 cameraPosition = camera.Position();
			glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
		}
		if (specularIntensityLocation >= 0)
			glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
		if (shininessLocation >= 0)
			glUniform1f(shininessLocation, globalSettings.lighting.shininess);
		if (saturationLocation >= 0)
			glUniform1f(saturationLocation, globalSettings.colorSaturation);
		if (specularScaleLocation >= 0)
			glUniform1f(specularScaleLocation, 1.0f);
		if (bondRadiusMultiplierLocation >= 0)
			glUniform1f(bondRadiusMultiplierLocation, globalSettings.bondRadiusMultiplier);
		if (sceneOffsetLocation >= 0)
			glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

		glBindVertexArray(m_CylinderMesh.vao);
		glDrawElementsInstanced(
			GL_TRIANGLES,
			m_CylinderMesh.indexCount,
			GL_UNSIGNED_INT,
			nullptr,
			static_cast<int>(resources.cachedBondInstances.size()));
		glBindVertexArray(0);
		resources.bondsDirty = false;
	}

	// Right/up in-plane basis for an Arrow2D quad: right follows the arrow's own direction projected
	// onto the given plane (so the quad points where the arrow points, same intent as
	// PinnedMeasurement::alignToBondDirection for labels), up is perpendicular within that same
	// plane. Used for both orientations - Billboard passes the camera's forward vector as
	// planeNormal, FixedPlane passes the chosen world axis - so this has no camera-specific logic of
	// its own, and the sign of planeNormal doesn't matter (a flat color quad is symmetric).
	void ComputeArrowQuadBasis(
		const glm::vec3 &direction,
		const glm::vec3 &planeNormal,
		glm::vec3 &outRight,
		glm::vec3 &outUp)
	{
		const glm::vec3 projected = direction - planeNormal * glm::dot(direction, planeNormal);
		glm::vec3 fallbackRight = glm::vec3(1.0f, 0.0f, 0.0f);
		if (std::abs(glm::dot(planeNormal, fallbackRight)) > 0.97f)
			fallbackRight = glm::vec3(0.0f, 1.0f, 0.0f);
		outRight = SafeNormalize(projected, fallbackRight);
		outUp = SafeNormalize(glm::cross(planeNormal, outRight), glm::vec3(0.0f, 0.0f, 1.0f));
	}

	void OpenGlRendererBackend::renderSceneArrows(
		const std::vector<RendererWindowState::SceneArrow> &arrows,
		const std::vector<std::size_t> &selectedArrows,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		bool renderArrow2D,
		const glm::vec2 &viewportPixelSize,
		const glm::vec3 &sceneOffset)
	{
		using ArrowKind = RendererWindowState::ArrowKind;
		using Arrow2DOrientation = RendererWindowState::Arrow2DOrientation;
		using WorldPlane = RendererWindowState::WorldPlane;

		const glm::mat4 view = camera.ViewMatrix();
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * view;
		const glm::vec3 cameraForward(view[0][2], view[1][2], view[2][2]);

		// Local rather than SelectionHitTest::ProjectToScreen - needs the raw clip.w test inline and
		// returns a plain glm::vec2 for the pixel-delta arithmetic right below each call.
		auto projectToPixels = [&](const glm::vec3 &world, glm::vec2 &outPixels) -> bool {
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (clip.w <= 0.0001f)
				return false;
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			outPixels = glm::vec2(
				(ndc.x * 0.5f + 0.5f) * viewportPixelSize.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportPixelSize.y);
			return true;
		};

		// Arrow3D mesh cache is indexed by position in `arrows` - synced to arrows.size() up front
		// (shrink frees GL objects for the dropped tail; grow must also happen here, never lazily
		// inside the loop below) so std::vector::resize's potential reallocation can never invalidate
		// an `Arrow3DDrawJob::mesh` pointer captured for an earlier index in the same call - e.g.
		// pasting 2 new Arrow3D arrows used to grow this vector one index at a time mid-loop, moving
		// already-queued jobs' mesh pointers to freed memory and feeding glDrawElementsInstanced a
		// garbage index count (GL_INVALID_VALUE/GL_INVALID_OPERATION). Runs on every call (this
		// function fires twice per frame) but is a no-op once already in sync - cheap size compare,
		// not worth guarding further.
		if (resources.sceneArrow3DMeshCache.size() > arrows.size())
		{
			for (std::size_t i = arrows.size(); i < resources.sceneArrow3DMeshCache.size(); ++i)
				DeleteMeshHandles(resources.sceneArrow3DMeshCache[i].mesh);
			resources.sceneArrow3DMeshCache.resize(arrows.size());
		}
		else if (resources.sceneArrow3DMeshCache.size() < arrows.size())
		{
			resources.sceneArrow3DMeshCache.resize(arrows.size());
		}

		std::vector<OpenGlBondInstance> shaftInstances;
		std::vector<OpenGlArrowQuadInstance> quadInstances;
		shaftInstances.reserve(arrows.size());
		// One draw job per Arrow3D - each has its own welded mesh (see OpenGlSceneArrowMeshCache), so
		// unlike shaftInstances/quadInstances above these can't be batched into a single instanced
		// draw call across arrows.
		struct Arrow3DDrawJob
		{
			const OpenGlMeshHandles *mesh;
			OpenGlBondInstance instance;
		};
		std::vector<Arrow3DDrawJob> arrow3DJobs;
		// Whole Line/Arrow3D batch drawn with depth writes off if any one arrow is translucent (see the
		// draw block below) - simple unsorted transparency, not a sorted system, per
		// docs/scene_arrow_rework_plan_corrected.md Step 8's "keep it simple" option.
		bool anyTransparent = false;

		for (std::size_t arrowIndex = 0; arrowIndex < arrows.size(); ++arrowIndex)
		{
			const RendererWindowState::SceneArrow &arrow = arrows[arrowIndex];
			// Two passes per frame (see this function's declaration comment) - each skips the kind it
			// doesn't own so building/uploading/drawing the other kind's instances never happens twice.
			const bool isArrow2D = arrow.kind == ArrowKind::Arrow2D;
			if (isArrow2D != renderArrow2D)
				continue;

			const glm::vec3 axis = arrow.end - arrow.start;
			const float length = glm::length(axis);
			if (!std::isfinite(length) || length <= 0.0001f)
				continue;
			const glm::vec3 direction = axis / length;
			const RendererWindowState::ArrowStyle &style = arrow.style;
			// Same accent/blend as atom and bond selection highlighting - SceneArrow never had an
			// equivalent before (RendererPanel's hit-test/drag already worked, but nothing ever showed
			// which arrow that state referred to).
			constexpr glm::vec3 kSelectionHighlightColor(0.91f, 0.52f, 0.02f);
			const bool isSelected =
				std::find(selectedArrows.begin(), selectedArrows.end(), arrowIndex) != selectedArrows.end();
			const glm::vec4 color(
				isSelected ? glm::mix(style.color, kSelectionHighlightColor, 0.55f) : style.color, style.alpha);

			if (isArrow2D)
			{
				const glm::vec3 worldCenter = (arrow.start + arrow.end) * 0.5f;
				const glm::vec3 planeNormal = arrow.orientation2D == Arrow2DOrientation::Billboard
					? cameraForward
					: arrow.fixedPlane == WorldPlane::XY   ? glm::vec3(0.0f, 0.0f, 1.0f)
					: arrow.fixedPlane == WorldPlane::XZ   ? glm::vec3(0.0f, 1.0f, 0.0f)
															: glm::vec3(1.0f, 0.0f, 0.0f);
				glm::vec3 right(0.0f), up(0.0f);
				ComputeArrowQuadBasis(direction, planeNormal, right, up);

				// ArrowStyle's shaftWidth/headWidth/headLength/outlineWidth are screen-space pixels
				// for every Arrow2D orientation (docs/scene_arrow_rework_plan_corrected.md Section 8)
				// - convert to this arrow's own local world-space scale via a projection probe, same
				// pixel<->world idea RendererPanel::handleFreeLabelInteraction already uses for its
				// drag delta. Skip the arrow (rather than divide by ~0) if any of the three points
				// needed for the probe project behind the camera.
				glm::vec2 centerPixels(0.0f), rightProbePixels(0.0f), upProbePixels(0.0f);
				if (!projectToPixels(worldCenter, centerPixels) ||
					!projectToPixels(worldCenter + right, rightProbePixels) ||
					!projectToPixels(worldCenter + up, upProbePixels))
					continue;
				const float worldPerPixelRight = 1.0f / std::max(glm::length(rightProbePixels - centerPixels), 0.0001f);
				const float worldPerPixelUp = 1.0f / std::max(glm::length(upProbePixels - centerPixels), 0.0001f);

				// Clamped so a long requested head doesn't consume more than half a short arrow.
				const float headLengthWorld = std::min(style.headLength * worldPerPixelRight, 0.45f * length);

				OpenGlArrowQuadInstance quad;
				quad.worldCenter = worldCenter;
				quad.right = right;
				quad.up = up;
				quad.halfSize = glm::vec2(length * 0.5f, style.shaftWidth * 0.5f * worldPerPixelUp);
				quad.color = color;
				quad.outlineColor = style.outlineColor;
				quad.outlineWidth = style.outlineWidth * worldPerPixelUp;
				quad.headHalfWidth = style.headWidth * 0.5f * worldPerPixelUp;
				quad.headLength = headLengthWorld;
				quadInstances.push_back(quad);
				continue;
			}

			if (color.a < 0.999f)
				anyTransparent = true;

			// shaftWidth/headWidth are full diameters (docs/scene_arrow_rework_plan_corrected.md
			// Section 8's global semantic rule) - buildBondTransform/BuildWeldedArrowMesh want a radius.
			const float shaftRadius = 0.5f * style.shaftWidth;

			if (arrow.kind == ArrowKind::Line)
			{
				OpenGlBondInstance shaft;
				shaft.model = buildBondTransform(arrow.start, arrow.end, shaftRadius);
				shaft.colorA = color;
				shaft.colorB = color;
				shaftInstances.push_back(shaft);
				continue;
			}

			// Arrow3D: one welded shaft+head mesh per arrow (BuildWeldedArrowMesh) instead of the
			// shared cylinder+cone instanced separately - see that function's declaration comment for
			// why two independently-instanced meshes could never share a seam vertex/normal. Head
			// capped at 0.6*length (not the whole arrow) so a short arrow always keeps a visible shaft.
			const float headRadius = 0.5f * style.headWidth;
			const float headLength = std::min(style.headLength, length * 0.6f);

			// No resize here - the cache is already sized to arrows.size() above, before this loop
			// took any `&cacheEntry.mesh` addresses (see that block's comment for why resizing here
			// instead used to dangle earlier jobs' mesh pointers).
			OpenGlSceneArrowMeshCache &cacheEntry = resources.sceneArrow3DMeshCache[arrowIndex];
			if (cacheEntry.shaftRadius != shaftRadius || cacheEntry.headRadius != headRadius ||
				cacheEntry.headLength != headLength || cacheEntry.length != length ||
				cacheEntry.bulgeStrength != globalSettings.arrowHeadBulgeStrength)
			{
				constexpr std::uint32_t kArrow3DRadialSegments = 24u;
				const RefinedConeMesh welded = BuildWeldedArrowMesh(
					shaftRadius, headRadius, headLength, length, kArrow3DRadialSegments,
					globalSettings.arrowHeadBulgeStrength);
				UploadSceneArrowMesh(cacheEntry.mesh, welded);
				cacheEntry.shaftRadius = shaftRadius;
				cacheEntry.headRadius = headRadius;
				cacheEntry.headLength = headLength;
				cacheEntry.length = length;
				cacheEntry.bulgeStrength = globalSettings.arrowHeadBulgeStrength;
			}

			if (cacheEntry.mesh.indexCount > 0)
			{
				Arrow3DDrawJob job;
				job.mesh = &cacheEntry.mesh;
				// No length/radius scale - BuildWeldedArrowMesh already bakes absolute world-unit
				// dimensions into its vertices (see buildArrowRevolutionTransform's declaration comment).
				job.instance.model = buildArrowRevolutionTransform(arrow.start, arrow.end);
				job.instance.colorA = color;
				job.instance.colorB = color;
				arrow3DJobs.push_back(job);
			}
		}

		if (shaftInstances.empty() && arrow3DJobs.empty() && quadInstances.empty())
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.SceneArrows");
#endif

		if (!shaftInstances.empty() || !arrow3DJobs.empty())
		{
			const unsigned int program = m_ShaderLibrary.Program("bonds");
			if (program != 0)
			{
				if (!shaftInstances.empty())
				{
					glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.instanceVbo);
					const GLsizeiptr requiredBytes = static_cast<GLsizeiptr>(shaftInstances.size() * sizeof(OpenGlBondInstance));
					GLint currentSize = 0;
					glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
					if (static_cast<GLsizeiptr>(currentSize) < requiredBytes)
					{
						const GLsizeiptr newSize = requiredBytes + requiredBytes / 2;
						glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
					}
					glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, shaftInstances.data());
				}

				glUseProgram(program);
				const int viewProjectionLocation = m_ShaderLibrary.Uniform("bonds", "u_ViewProjection");
				if (viewProjectionLocation >= 0)
					glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
				const int keyDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_KeyDirection");
				const int fillDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_FillDirection");
				const int backDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_BackDirection");
				const int ambientLocation = m_ShaderLibrary.Uniform("bonds", "u_AmbientIntensity");
				const int keyIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_KeyIntensity");
				const int fillIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_FillIntensity");
				const int backIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_BackIntensity");
				const int twoSidedLocation = m_ShaderLibrary.Uniform("bonds", "u_TwoSidedLighting");
				const int cameraPositionLocation = m_ShaderLibrary.Uniform("bonds", "u_CameraPosition");
				const int specularIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_SpecularIntensity");
				const int shininessLocation = m_ShaderLibrary.Uniform("bonds", "u_Shininess");
				const int saturationLocation = m_ShaderLibrary.Uniform("bonds", "u_Saturation");
				const int specularScaleLocation = m_ShaderLibrary.Uniform("bonds", "u_SpecularScale");
				// Arrows are a fixed user-chosen color, not derived from AtomStyleTable like bonds -
				// the thickness/saturation sliders (RendererGlobalRenderSettings) still apply for
				// visual consistency with the rest of the scene, same uniforms renderBonds uploads.
				const int bondRadiusMultiplierLocation = m_ShaderLibrary.Uniform("bonds", "u_BondRadiusMultiplier");
				const int sceneOffsetLocation = m_ShaderLibrary.Uniform("bonds", "u_SceneOffset");
				if (keyDirectionLocation >= 0)
					glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
				if (fillDirectionLocation >= 0)
					glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
				if (backDirectionLocation >= 0)
					glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
				if (ambientLocation >= 0)
					glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
				if (keyIntensityLocation >= 0)
					glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
				if (fillIntensityLocation >= 0)
					glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
				if (backIntensityLocation >= 0)
					glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
				if (twoSidedLocation >= 0)
					glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
				if (cameraPositionLocation >= 0)
				{
					const glm::vec3 cameraPosition = camera.Position();
					glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
				}
				if (specularIntensityLocation >= 0)
					glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
				if (shininessLocation >= 0)
					glUniform1f(shininessLocation, globalSettings.lighting.shininess);
				if (saturationLocation >= 0)
					glUniform1f(saturationLocation, globalSettings.colorSaturation);
				if (specularScaleLocation >= 0)
					glUniform1f(specularScaleLocation, 0.25f);
				if (bondRadiusMultiplierLocation >= 0)
					glUniform1f(bondRadiusMultiplierLocation, globalSettings.bondRadiusMultiplier);
				if (sceneOffsetLocation >= 0)
					glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

				// Alpha blending on translucent arrows still writes depth by default, which would let a
				// see-through arrow wrongly occlude whatever is behind it. Not a full sorted-transparency
				// system (docs/scene_arrow_rework_plan_corrected.md Step 8 explicitly rules that out for
				// this rework) - just depth writes off for the pass, restored right after, same
				// GL_BLEND/GL_DEPTH_TEST the rest of the scene already has enabled.
				if (anyTransparent)
					glDepthMask(GL_FALSE);

				if (!shaftInstances.empty())
				{
					glBindVertexArray(m_CylinderMesh.vao);
					glDrawElementsInstanced(
						GL_TRIANGLES, m_CylinderMesh.indexCount, GL_UNSIGNED_INT, nullptr,
						static_cast<int>(shaftInstances.size()));
				}
				// Each Arrow3D has its own mesh (different shaft/head proportions), so unlike the
				// shaft's shared m_CylinderMesh above, this can't be one instanced call across arrows -
				// one draw per job, instance count 1 (reuses the exact same instanced VAO layout/
				// uniforms, just with a single-element instance buffer instead of a batch).
				for (const Arrow3DDrawJob &job : arrow3DJobs)
				{
					glBindBuffer(GL_ARRAY_BUFFER, job.mesh->instanceVbo);
					glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(OpenGlBondInstance), &job.instance);
					glBindVertexArray(job.mesh->vao);
					glDrawElementsInstanced(GL_TRIANGLES, job.mesh->indexCount, GL_UNSIGNED_INT, nullptr, 1);
				}
				glBindVertexArray(0);

				if (anyTransparent)
					glDepthMask(GL_TRUE);
			}
		}

		if (!quadInstances.empty())
		{
			const unsigned int quadProgram = m_ShaderLibrary.Program("arrow_quad");
			if (quadProgram != 0)
			{
				glBindBuffer(GL_ARRAY_BUFFER, m_ArrowQuadMesh.instanceVbo);
				const GLsizeiptr requiredBytes = static_cast<GLsizeiptr>(quadInstances.size() * sizeof(OpenGlArrowQuadInstance));
				GLint currentSize = 0;
				glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
				if (static_cast<GLsizeiptr>(currentSize) < requiredBytes)
				{
					const GLsizeiptr newSize = requiredBytes + requiredBytes / 2;
					glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
				}
				glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, quadInstances.data());

				glUseProgram(quadProgram);
				const int viewProjectionLocation = m_ShaderLibrary.Uniform("arrow_quad", "u_ViewProjection");
				if (viewProjectionLocation >= 0)
					glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
				const int sceneOffsetLocation = m_ShaderLibrary.Uniform("arrow_quad", "u_SceneOffset");
				if (sceneOffsetLocation >= 0)
					glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

				// FixedPlane orientation picks a constant world-axis normal regardless of which side
				// the camera ends up on (Billboard's camera-facing normal always faces the viewer by
				// construction, but a fixed plane's does not) - back-face culling was silently
				// discarding the whole quad from the "wrong" side, same fix already applied to the
				// isosurface's translucent shell below. Depth test is also off for this pass -
				// Arrow2D is drawn late, alongside labels (see this function's declaration comment /
				// RenderWindow), so an annotation always reads on top regardless of where it's
				// anchored in 3D.
				glDisable(GL_CULL_FACE);
				glDisable(GL_DEPTH_TEST);
				glBindVertexArray(m_ArrowQuadMesh.vao);
				glDrawElementsInstanced(
					GL_TRIANGLES, m_ArrowQuadMesh.indexCount, GL_UNSIGNED_INT, nullptr,
					static_cast<int>(quadInstances.size()));
				glBindVertexArray(0);
				glEnable(GL_DEPTH_TEST);
				glEnable(GL_CULL_FACE);
			}
		}
	}

	// Auto bond-length labels, one MSDF billboard per bond, always regenerated from live atom
	// positions (no separate ECS label entity yet - see docs/work/project/TODO.md T09 for why that
	// part is deferred to Etap F alongside real selection-mode support). Runs inside RenderWindow
	// like every other geometry pass, so it's automatically part of whatever CaptureWindowToPng
	// reads back - no separate export wiring needed.
	void OpenGlRendererBackend::renderLabels(
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		bool showAllLabels,
		const std::vector<RendererWindowState::PinnedMeasurement> &pinnedMeasurements,
		const std::vector<std::size_t> &selectedPinnedMeasurements,
		const std::vector<RendererWindowState::FreeLabel> &freeLabels,
		const std::vector<std::size_t> &selectedFreeLabels,
		const glm::vec3 &sceneOffset)
	{
		if (m_LabelFont == nullptr)
		{
			m_LabelFont = CreateUnique<MsdfFont>(resolveLabelFontPath());
			if (!m_LabelFont->IsValid())
				DS_LOG_WARN("Renderer: label font failed to load, bond-length labels will not render");
		}
		if (!m_LabelFont->IsValid())
			return;

		// Two independent triggers (Alt+M "every bond" vs M "pin/unpin the current selection") share
		// the same draw path but not the same data source: "every bond" is expensive enough to cache
		// (resources.cachedLabelInstances, invalidated by labelsDirty) while pinned measurements are
		// few and cheap (a handful of glyphs each) to just rebuild every frame - adding a second
		// dirty-tracking axis keyed on the pin list would cost more than it saves here.
		std::vector<OpenGlLabelInstance> pinnedInstances;
		std::vector<OpenGlLabelInstance> pinnedBackgroundInstances;
		const std::vector<OpenGlLabelInstance> *instancesToDraw = nullptr;
		const std::vector<OpenGlLabelInstance> *backgroundInstancesToDraw = nullptr;

		if (showAllLabels)
		{
			if (resources.labelsDirty)
			{
				resources.cachedLabelInstances.clear();
				resources.cachedLabelBackgroundInstances.clear();
				const RendererWindowState::LabelStyle kDefaultBondLabelStyle;
				for (const RendererBondData &bond : structure.bonds)
				{
					if (!bond.visible)
						continue;
					if (bond.firstAtomIndex >= structure.atoms.size() || bond.secondAtomIndex >= structure.atoms.size())
						continue;
					const RendererAtomData &first = structure.atoms[bond.firstAtomIndex];
					const RendererAtomData &second = structure.atoms[bond.secondAtomIndex];
					if (!first.visible || !second.visible)
						continue;

					const glm::vec3 secondPosition = second.cartesianPosition + bond.secondAtomPeriodicOffset;
					const float lengthAngstrom = glm::length(secondPosition - first.cartesianPosition);
					const glm::vec3 midpoint = (first.cartesianPosition + secondPosition) * 0.5f;
					const LabelLocalBounds bounds = AppendBondLabelInstances(
						*m_LabelFont, midpoint, lengthAngstrom, resources.cachedLabelInstances, kDefaultBondLabelStyle);
					AppendLabelBackgroundInstance(
						midpoint, bounds, kDefaultBondLabelStyle, 0.0f, resources.cachedLabelBackgroundInstances);
				}
				resources.labelsDirty = false;
			}
			instancesToDraw = &resources.cachedLabelInstances;
			backgroundInstancesToDraw = &resources.cachedLabelBackgroundInstances;
		}
		else if (!pinnedMeasurements.empty() || !freeLabels.empty())
		{
			// worldOffset (see PinnedMeasurement) is a fixed 3D world-space nudge as of Etap F, not a
			// camera-relative one - a dragged-apart label now stays put in world space regardless of
			// how the camera orbits afterwards (the old screenOffset predecessor swam when orbiting;
			// see RendererWindowState.hpp's comment on worldOffset for how it is still written to via
			// the same camera-right/up projection during a plain click-drag). cameraRight/cameraUp are
			// still needed below for bond-direction label alignment, independent of the offset itself.
			const glm::mat4 offsetView = camera.ViewMatrix();
			const glm::vec3 cameraRight(offsetView[0][0], offsetView[1][0], offsetView[2][0]);
			const glm::vec3 cameraUp(offsetView[0][1], offsetView[1][1], offsetView[2][1]);

			for (std::size_t pinIndex = 0; pinIndex < pinnedMeasurements.size(); ++pinIndex)
			{
				const RendererWindowState::PinnedMeasurement &pin = pinnedMeasurements[pinIndex];
				// Selection highlight overrides just the text color/alpha, layered on top of the
				// pin's own style (outline/background/padding/scale all still apply while selected).
				RendererWindowState::LabelStyle effectiveStyle = pin.style;
				if (std::find(selectedPinnedMeasurements.begin(), selectedPinnedMeasurements.end(), pinIndex) !=
					selectedPinnedMeasurements.end())
				{
					effectiveStyle.textColor = kPinnedLabelSelectedColor;
					effectiveStyle.textAlpha = 1.0f;
				}
				const glm::vec3 offset = pin.worldOffset;

				if (pin.atomIndices.size() == 2)
				{
					const std::size_t atomA = pin.atomIndices[0];
					const std::size_t atomB = pin.atomIndices[1];
					if (atomA >= structure.atoms.size() || atomB >= structure.atoms.size())
						continue;

					// Shared by both the real-bond match below and the no-bond fallback beneath it -
					// only the two endpoint positions differ between them.
					auto appendLengthLabel = [&](const glm::vec3 &posA, const glm::vec3 &posB)
					{
						const float lengthAngstrom = glm::length(posB - posA);
						const glm::vec3 midpoint = (posA + posB) * 0.5f;

						float rotationRadians = 0.0f;
						if (pin.alignToBondDirection)
						{
							// In-plane angle of the bond direction within the billboard's own basis
							// (project onto cameraRight/cameraUp, not screen space - keeps the label
							// readable without perspective skew at extreme angles).
							const glm::vec3 bondDir = posB - posA;
							const float dx = glm::dot(bondDir, cameraRight);
							const float dy = glm::dot(bondDir, cameraUp);
							if (dx != 0.0f || dy != 0.0f)
							{
								rotationRadians = std::atan2(dy, dx);
								// Keep the label upright regardless of which way the bond points on
								// screen - a lattice has bonds pointing every which way, and raw
								// bond-angle alignment left roughly half of them upside-down/sideways.
								while (rotationRadians > glm::half_pi<float>())
									rotationRadians -= glm::pi<float>();
								while (rotationRadians <= -glm::half_pi<float>())
									rotationRadians += glm::pi<float>();
							}
							if (pin.flipped)
								rotationRadians += glm::pi<float>();
						}
						const float totalRotation = rotationRadians + pin.rotationOffsetRadians;
						const LabelLocalBounds bounds = AppendBondLabelInstances(
							*m_LabelFont, midpoint + offset, lengthAngstrom, pinnedInstances, effectiveStyle,
							totalRotation);
						AppendLabelBackgroundInstance(
							midpoint + offset, bounds, effectiveStyle, totalRotation, pinnedBackgroundInstances);
					};

					constexpr float kPeriodicOffsetEpsilon = 1.0e-3f;
					bool matchedBond = false;
					for (const RendererBondData &bond : structure.bonds)
					{
						// Two atoms can be joined by more than one real bond across different periodic
						// images (see PinnedMeasurement::bondPeriodicOffset) - matching only the atom
						// pair and taking the first hit would render this pin at whichever bond happened
						// to come first in the list, regardless of which one was actually pinned.
						glm::vec3 offsetTowardB(0.0f);
						bool matches = false;
						if (bond.firstAtomIndex == atomA && bond.secondAtomIndex == atomB)
						{
							offsetTowardB = bond.secondAtomPeriodicOffset;
							matches = true;
						}
						else if (bond.firstAtomIndex == atomB && bond.secondAtomIndex == atomA)
						{
							offsetTowardB = -bond.secondAtomPeriodicOffset;
							matches = true;
						}
						if (!matches || glm::distance(offsetTowardB, pin.bondPeriodicOffset) >= kPeriodicOffsetEpsilon)
							continue;
						const RendererAtomData &first = structure.atoms[bond.firstAtomIndex];
						const RendererAtomData &second = structure.atoms[bond.secondAtomIndex];
						appendLengthLabel(first.cartesianPosition, second.cartesianPosition + bond.secondAtomPeriodicOffset);
						matchedBond = true;
						break;
					}
					// Arbitrary pair with no matching bond (Measure tool's "any 2 atoms" fallback pin,
					// see AddBondPinsWithinSet) - no periodic image to resolve, just the raw positions.
					// Without this the pin existed in pinnedMeasurements but never rendered anything,
					// since the loop above only ever produces a label for an actual RendererBondData.
					if (!matchedBond)
						appendLengthLabel(structure.atoms[atomA].cartesianPosition, structure.atoms[atomB].cartesianPosition);
				}
				else if (pin.atomIndices.size() == 3)
				{
					const bool inRange = std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(), [&](const std::size_t index) {
						return index < structure.atoms.size();
					});
					if (!inRange)
						continue;

					const std::size_t vertexIndex = ResolveAngleVertexIndex(structure, pin.atomIndices);
					const RendererAtomData &vertexAtom = structure.atoms[vertexIndex];
					const RendererAtomData &sideAtomA =
						structure.atoms[pin.atomIndices[0] == vertexIndex ? pin.atomIndices[1] : pin.atomIndices[0]];
					const RendererAtomData &sideAtomC =
						structure.atoms[pin.atomIndices[2] == vertexIndex ? pin.atomIndices[1] : pin.atomIndices[2]];
					const glm::vec3 toA = sideAtomA.cartesianPosition - vertexAtom.cartesianPosition;
					const glm::vec3 toC = sideAtomC.cartesianPosition - vertexAtom.cartesianPosition;
					const float lengthA = glm::length(toA);
					const float lengthC = glm::length(toC);
					if (lengthA > 0.0001f && lengthC > 0.0001f)
					{
						const float cosAngle = glm::clamp(glm::dot(toA, toC) / (lengthA * lengthC), -1.0f, 1.0f);
						const float angleDeg = glm::degrees(std::acos(cosAngle));
						const glm::vec3 anglePosition = vertexAtom.cartesianPosition + offset;
						const LabelLocalBounds bounds = AppendAngleLabelInstances(
							*m_LabelFont, anglePosition, angleDeg, pinnedInstances, effectiveStyle,
							pin.rotationOffsetRadians);
						AppendLabelBackgroundInstance(
							anglePosition, bounds, effectiveStyle, pin.rotationOffsetRadians, pinnedBackgroundInstances);
					}
				}
			}
			for (std::size_t labelIndex = 0; labelIndex < freeLabels.size(); ++labelIndex)
			{
				const RendererWindowState::FreeLabel &label = freeLabels[labelIndex];
				RendererWindowState::LabelStyle effectiveStyle = label.style;
				if (std::find(selectedFreeLabels.begin(), selectedFreeLabels.end(), labelIndex) !=
					selectedFreeLabels.end())
				{
					effectiveStyle.textColor = kPinnedLabelSelectedColor;
					effectiveStyle.textAlpha = 1.0f;
				}
				const LabelLocalBounds bounds = AppendLabelInstances(
					*m_LabelFont, label.worldPosition, ToU32String(label.text), pinnedInstances, effectiveStyle,
					label.rotationRadians);
				AppendLabelBackgroundInstance(
					label.worldPosition, bounds, effectiveStyle, label.rotationRadians, pinnedBackgroundInstances);
			}
			instancesToDraw = &pinnedInstances;
			backgroundInstancesToDraw = &pinnedBackgroundInstances;
		}

		if (instancesToDraw == nullptr || instancesToDraw->empty())
			return;

		const glm::mat4 view = camera.ViewMatrix();
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * view;

		// Labels always render on top of the scene (atoms/bonds/cell box/isosurfaces) rather than
		// depth-testing against it - an annotation anchored inside a structure (e.g. a free label
		// placed at the origin of a cluster of atoms) would otherwise get partially carved up by
		// whatever 3D geometry happens to be nearer the camera at that point, which reads as a
		// rendering glitch rather than the "3D label" effect it technically is. RenderWindow already
		// draws labels last (after atoms/isosurfaces) so this only needs to disable testing, not
		// worry about later passes drawing over these pixels. Depth test still applies between the
		// background quad and glyph pass below via plain draw order (background first), so no
		// z-fighting risk even without the depth buffer's help.
		glDisable(GL_DEPTH_TEST);

		if (backgroundInstancesToDraw != nullptr && !backgroundInstancesToDraw->empty())
		{
			const unsigned int backgroundProgram = m_ShaderLibrary.Program("label_background");
			if (backgroundProgram != 0)
			{
#if defined(TRACY_ENABLE)
				TracyGpuZone("Renderer.LabelBackgrounds");
#endif
				glBindBuffer(GL_ARRAY_BUFFER, m_LabelQuadMesh.instanceVbo);
				const GLsizeiptr requiredBackgroundBytes =
					static_cast<GLsizeiptr>(backgroundInstancesToDraw->size() * sizeof(OpenGlLabelInstance));
				GLint currentBackgroundSize = 0;
				glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentBackgroundSize);
				if (static_cast<GLsizeiptr>(currentBackgroundSize) < requiredBackgroundBytes)
				{
					const GLsizeiptr newSize = requiredBackgroundBytes + requiredBackgroundBytes / 2;
					glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
				}
				glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBackgroundBytes, backgroundInstancesToDraw->data());

				glUseProgram(backgroundProgram);
				const int bgViewProjectionLocation = m_ShaderLibrary.Uniform("label_background", "u_ViewProjection");
				if (bgViewProjectionLocation >= 0)
					glUniformMatrix4fv(bgViewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
				const int bgViewLocation = m_ShaderLibrary.Uniform("label_background", "u_View");
				if (bgViewLocation >= 0)
					glUniformMatrix4fv(bgViewLocation, 1, GL_FALSE, &view[0][0]);
				const int bgSceneOffsetLocation = m_ShaderLibrary.Uniform("label_background", "u_SceneOffset");
				if (bgSceneOffsetLocation >= 0)
					glUniform3fv(bgSceneOffsetLocation, 1, &sceneOffset.x);

				glBindVertexArray(m_LabelQuadMesh.vao);
				glDrawElementsInstanced(
					GL_TRIANGLES,
					m_LabelQuadMesh.indexCount,
					GL_UNSIGNED_INT,
					nullptr,
					static_cast<int>(backgroundInstancesToDraw->size()));
				glBindVertexArray(0);
			}
		}

		const unsigned int program = m_ShaderLibrary.Program("labels");
		if (program == 0)
		{
			glEnable(GL_DEPTH_TEST);
			return;
		}

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Labels");
#endif

		glBindBuffer(GL_ARRAY_BUFFER, m_LabelQuadMesh.instanceVbo);
		const GLsizeiptr requiredBytes =
			static_cast<GLsizeiptr>(instancesToDraw->size() * sizeof(OpenGlLabelInstance));
		GLint currentSize = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
		if (static_cast<GLsizeiptr>(currentSize) < requiredBytes)
		{
			const GLsizeiptr newSize = requiredBytes + requiredBytes / 2;
			glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
		}
		glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, instancesToDraw->data());

		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("labels", "u_ViewProjection");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		const int viewLocation = m_ShaderLibrary.Uniform("labels", "u_View");
		if (viewLocation >= 0)
			glUniformMatrix4fv(viewLocation, 1, GL_FALSE, &view[0][0]);
		const int pixelRangeLocation = m_ShaderLibrary.Uniform("labels", "u_PixelRange");
		if (pixelRangeLocation >= 0)
			glUniform1f(pixelRangeLocation, static_cast<float>(m_LabelFont->PixelRange()));
		const int sceneOffsetLocation = m_ShaderLibrary.Uniform("labels", "u_SceneOffset");
		if (sceneOffsetLocation >= 0)
			glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_LabelFont->AtlasTextureId());
		const int atlasLocation = m_ShaderLibrary.Uniform("labels", "u_AtlasTexture");
		if (atlasLocation >= 0)
			glUniform1i(atlasLocation, 0);

		glBindVertexArray(m_LabelQuadMesh.vao);
		glDrawElementsInstanced(
			GL_TRIANGLES,
			m_LabelQuadMesh.indexCount,
			GL_UNSIGNED_INT,
			nullptr,
			static_cast<int>(instancesToDraw->size()));
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glEnable(GL_DEPTH_TEST);
	}

	void OpenGlRendererBackend::renderCellBox(
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const glm::vec3 &sceneOffset)
	{
		if (resources.cellEdgesDirty)
		{
			resources.cachedCellEdgeVertices.clear();
			resources.cachedCellEdgeVertices.reserve(structure.cellEdges.size() * 2);
			for (const RendererCellEdge &edge : structure.cellEdges)
			{
				resources.cachedCellEdgeVertices.push_back(edge.start);
				resources.cachedCellEdgeVertices.push_back(edge.finish);
			}
			resources.cellEdgesDirty = false;
		}

		if (resources.cachedCellEdgeVertices.empty())
			return;

		const auto &vertices = resources.cachedCellEdgeVertices;

		const unsigned int program = m_ShaderLibrary.Program("lines");
		if (program == 0)
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.CellBox");
#endif

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("lines", "u_ViewProjection");
		const int colorLocation = m_ShaderLibrary.Uniform("lines", "u_LineColor");
		const int sceneOffsetLocation = m_ShaderLibrary.Uniform("lines", "u_SceneOffset");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		if (colorLocation >= 0)
			glUniform4f(colorLocation, 0.85f, 0.85f, 0.9f, 1.0f);
		if (sceneOffsetLocation >= 0)
			glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);
		glLineWidth(1.0f);

		glBindVertexArray(m_LineVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(glm::vec3)), vertices.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_LINES, 0, static_cast<int>(vertices.size()));
		glBindVertexArray(0);
	}

	void OpenGlRendererBackend::renderGrid(
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset)
	{
		if (resources.gridDirty)
		{
			resources.cachedGridVertices.clear();

			const float spacing = std::max(globalSettings.grid.spacing, 0.01f);
			glm::vec3 minimum(0.0f, 0.0f, globalSettings.grid.planeZ);
			glm::vec3 maximum(0.0f, 0.0f, globalSettings.grid.planeZ);
			bool hasBounds = false;
			if (globalSettings.grid.autoFitToStructureBounds)
			{
				for (const RendererCellEdge &edge : structure.cellEdges)
				{
					if (!IsFiniteVec3(edge.start) || !IsFiniteVec3(edge.finish))
						continue;

					if (!hasBounds)
					{
						minimum = edge.start;
						maximum = edge.start;
						hasBounds = true;
					}

					minimum = glm::min(minimum, edge.start);
					minimum = glm::min(minimum, edge.finish);
					maximum = glm::max(maximum, edge.start);
					maximum = glm::max(maximum, edge.finish);
				}
			}

			const glm::vec3 center = hasBounds
				? glm::vec3(
					0.5f * (minimum.x + maximum.x),
					0.5f * (minimum.y + maximum.y),
					0.5f * (minimum.z + maximum.z) + globalSettings.grid.planeZ)
				: glm::vec3(0.0f, 0.0f, globalSettings.grid.planeZ);
			float halfSpan = spacing * 20.0f;
			if (hasBounds)
			{
				const float spanX = std::max(maximum.x - minimum.x, spacing);
				const float spanY = std::max(maximum.y - minimum.y, spacing);
				const float baseSpan = std::max(spanX, spanY);
				const float paddingFactor = 1.0f + std::max(globalSettings.grid.paddingPercent, 0.0f) * 0.01f;
				halfSpan = std::max(0.5f * baseSpan * paddingFactor, spacing);
			}

			const int halfLines = std::max(1, static_cast<int>(std::ceil(halfSpan / spacing)));
			const float clampedHalfSpan = static_cast<float>(halfLines) * spacing;
			for (int line = -halfLines; line <= halfLines; ++line)
			{
				const float coordinate = static_cast<float>(line) * spacing;
				resources.cachedGridVertices.push_back(glm::vec3(center.x - clampedHalfSpan, center.y + coordinate, center.z));
				resources.cachedGridVertices.push_back(glm::vec3(center.x + clampedHalfSpan, center.y + coordinate, center.z));
				resources.cachedGridVertices.push_back(glm::vec3(center.x + coordinate, center.y - clampedHalfSpan, center.z));
				resources.cachedGridVertices.push_back(glm::vec3(center.x + coordinate, center.y + clampedHalfSpan, center.z));
			}
		}
		if (resources.cachedGridVertices.empty())
			return;

		const unsigned int program = m_ShaderLibrary.Program("grid");
		if (program == 0)
			return;

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("grid", "u_ViewProjection");
		const int sceneOffsetLocation = m_ShaderLibrary.Uniform("grid", "u_SceneOffset");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		if (sceneOffsetLocation >= 0)
			glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);
		glBindVertexArray(m_LineVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVbo);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<long long>(resources.cachedGridVertices.size() * sizeof(glm::vec3)),
			resources.cachedGridVertices.data(),
			GL_DYNAMIC_DRAW);
		glDrawArrays(GL_LINES, 0, static_cast<int>(resources.cachedGridVertices.size()));
		glBindVertexArray(0);
		resources.gridDirty = false;
	}

	void OpenGlRendererBackend::renderIsosurfaceOverlay(
		const std::vector<IsosurfaceVertex> &vertices,
		const RendererViewCamera &camera,
		const RendererGlobalRenderSettings &globalSettings)
	{
		const unsigned int program = m_ShaderLibrary.Program("isosurface");
		if (program == 0)
			return;

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_ViewProjection");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		const int keyDirectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_KeyDirection");
		const int fillDirectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_FillDirection");
		const int backDirectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_BackDirection");
		const int ambientLocation = m_ShaderLibrary.Uniform("isosurface", "u_AmbientIntensity");
		const int keyIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_KeyIntensity");
		const int fillIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_FillIntensity");
		const int backIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_BackIntensity");
		const int twoSidedLocation = m_ShaderLibrary.Uniform("isosurface", "u_TwoSidedLighting");
		const int cameraPositionLocation = m_ShaderLibrary.Uniform("isosurface", "u_CameraPosition");
		const int specularIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_SpecularIntensity");
		const int shininessLocation = m_ShaderLibrary.Uniform("isosurface", "u_Shininess");
		const int rimIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_RimIntensity");
		const int rimPowerLocation = m_ShaderLibrary.Uniform("isosurface", "u_RimPower");
		const int saturationLocation = m_ShaderLibrary.Uniform("isosurface", "u_Saturation");
		if (keyDirectionLocation >= 0)
			glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
		if (fillDirectionLocation >= 0)
			glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
		if (backDirectionLocation >= 0)
			glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
		if (ambientLocation >= 0)
			glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
		if (keyIntensityLocation >= 0)
			glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
		if (fillIntensityLocation >= 0)
			glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
		if (backIntensityLocation >= 0)
			glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
		if (twoSidedLocation >= 0)
			glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
		if (cameraPositionLocation >= 0)
		{
			const glm::vec3 cameraPosition = camera.Position();
			glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
		}
		if (specularIntensityLocation >= 0)
			glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
		if (shininessLocation >= 0)
			glUniform1f(shininessLocation, globalSettings.lighting.shininess);
		if (rimIntensityLocation >= 0)
			glUniform1f(rimIntensityLocation, globalSettings.lighting.rimIntensity);
		if (rimPowerLocation >= 0)
			glUniform1f(rimPowerLocation, globalSettings.lighting.rimPower);
		if (saturationLocation >= 0)
			glUniform1f(saturationLocation, globalSettings.colorSaturation);

		// Hardcoded debug colors/alpha (T08.6.3 will make these Control Panel sliders) - blue for
		// the positive lobe, orange-red for the negative lobe, a common orbital-visualization
		// convention (e.g. VESTA's default +/- isosurface colors).
		const int positiveLocation = m_ShaderLibrary.Uniform("isosurface", "u_PositiveLobeColor");
		const int negativeLocation = m_ShaderLibrary.Uniform("isosurface", "u_NegativeLobeColor");
		const int alphaLocation = m_ShaderLibrary.Uniform("isosurface", "u_LobeAlpha");
		if (positiveLocation >= 0)
			glUniform3f(positiveLocation, 0.25f, 0.55f, 0.95f);
		if (negativeLocation >= 0)
			glUniform3f(negativeLocation, 0.95f, 0.45f, 0.2f);
		if (alphaLocation >= 0)
			glUniform1f(alphaLocation, 0.6f);

		glBindVertexArray(m_IsosurfaceVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_IsosurfaceVbo);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<long long>(vertices.size() * sizeof(IsosurfaceVertex)),
			vertices.data(),
			GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(vertices.size()));
		glBindVertexArray(0);
	}

	void OpenGlRendererBackend::renderIsosurfaceGpuOverlay(
		unsigned int vao,
		int vertexCount,
		const RendererViewCamera &camera,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &positiveLobeColor,
		const glm::vec3 &negativeLobeColor,
		float lobeAlpha,
		const glm::vec3 &sceneOffset)
	{
		// Same "isosurface" shader/lighting/colors as the CPU overlay - only the vertex source
		// (resources.isosurfaceVao, filled by RegenerateIsosurfaceGpu) and draw count differ, so a
		// visual mismatch between the two overlays means the compute shader port has a bug, not
		// a rendering/lighting difference.
		const unsigned int program = m_ShaderLibrary.Program("isosurface");
		if (program == 0)
			return;

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_ViewProjection");
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		const int keyDirectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_KeyDirection");
		const int fillDirectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_FillDirection");
		const int backDirectionLocation = m_ShaderLibrary.Uniform("isosurface", "u_BackDirection");
		const int ambientLocation = m_ShaderLibrary.Uniform("isosurface", "u_AmbientIntensity");
		const int keyIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_KeyIntensity");
		const int fillIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_FillIntensity");
		const int backIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_BackIntensity");
		const int twoSidedLocation = m_ShaderLibrary.Uniform("isosurface", "u_TwoSidedLighting");
		const int cameraPositionLocation = m_ShaderLibrary.Uniform("isosurface", "u_CameraPosition");
		const int specularIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_SpecularIntensity");
		const int shininessLocation = m_ShaderLibrary.Uniform("isosurface", "u_Shininess");
		const int rimIntensityLocation = m_ShaderLibrary.Uniform("isosurface", "u_RimIntensity");
		const int rimPowerLocation = m_ShaderLibrary.Uniform("isosurface", "u_RimPower");
		const int saturationLocation = m_ShaderLibrary.Uniform("isosurface", "u_Saturation");
		const int sceneOffsetLocation = m_ShaderLibrary.Uniform("isosurface", "u_SceneOffset");
		if (keyDirectionLocation >= 0)
			glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
		if (fillDirectionLocation >= 0)
			glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
		if (backDirectionLocation >= 0)
			glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
		if (ambientLocation >= 0)
			glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
		if (keyIntensityLocation >= 0)
			glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
		if (fillIntensityLocation >= 0)
			glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
		if (backIntensityLocation >= 0)
			glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
		if (twoSidedLocation >= 0)
			glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
		if (cameraPositionLocation >= 0)
		{
			const glm::vec3 cameraPosition = camera.Position();
			glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
		}
		if (specularIntensityLocation >= 0)
			glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
		if (shininessLocation >= 0)
			glUniform1f(shininessLocation, globalSettings.lighting.shininess);
		if (rimIntensityLocation >= 0)
			glUniform1f(rimIntensityLocation, globalSettings.lighting.rimIntensity);
		if (rimPowerLocation >= 0)
			glUniform1f(rimPowerLocation, globalSettings.lighting.rimPower);
		if (saturationLocation >= 0)
			glUniform1f(saturationLocation, globalSettings.colorSaturation);
		if (sceneOffsetLocation >= 0)
			glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

		const int positiveLocation = m_ShaderLibrary.Uniform("isosurface", "u_PositiveLobeColor");
		const int negativeLocation = m_ShaderLibrary.Uniform("isosurface", "u_NegativeLobeColor");
		const int alphaLocation = m_ShaderLibrary.Uniform("isosurface", "u_LobeAlpha");
		if (positiveLocation >= 0)
			glUniform3fv(positiveLocation, 1, &positiveLobeColor.x);
		if (negativeLocation >= 0)
			glUniform3fv(negativeLocation, 1, &negativeLobeColor.x);
		if (alphaLocation >= 0)
			glUniform1f(alphaLocation, lobeAlpha);

		// A translucent surface should show its back faces too (otherwise back-face culling makes
		// it look like an opaque shell instead of a soft cloud) - disabled only for this draw call,
		// re-enabled immediately after so atoms/bonds keep their normal culling.
		glDisable(GL_CULL_FACE);
		glBindVertexArray(vao);
		glDrawArrays(GL_TRIANGLES, 0, vertexCount);
		glBindVertexArray(0);
		glEnable(GL_CULL_FACE);
	}

	int OpenGlRendererBackend::RegenerateIsosurfaceGpu(
		const std::string &windowKey, const OrbitalGridData &grid, float isoValue, int slot)
	{
		if (!m_Initialized)
			return 0;
		if (grid.dimensions.x < 2 || grid.dimensions.y < 2 || grid.dimensions.z < 2 || isoValue <= 0.0f)
			return 0;
		if (slot < 0 || slot >= kIsosurfaceSlotCount)
			return 0;

		const auto viewportIt = m_Viewports.find(windowKey);
		if (viewportIt == m_Viewports.end())
			return 0;
		OpenGlViewportResources &resources = viewportIt->second;
		ensureIsosurfaceBuffers(resources);

		const unsigned int program = m_ShaderLibrary.Program("isosurface_compute");
		if (program == 0)
			return 0;

		const std::size_t expectedValues =
			static_cast<std::size_t>(grid.dimensions.x) *
			static_cast<std::size_t>(grid.dimensions.y) *
			static_cast<std::size_t>(grid.dimensions.z);
		if (grid.values.size() != expectedValues)
			return 0;

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_IsosurfaceGridSsbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			static_cast<GLsizeiptr>(grid.values.size() * sizeof(float)),
			grid.values.data(), GL_STATIC_DRAW);

		const unsigned int zero = 0;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.isosurfaceCounterSsbo[slot]);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(unsigned int), &zero);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_IsosurfaceGridSsbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, resources.isosurfaceVertexSsbo[slot]);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, resources.isosurfaceCounterSsbo[slot]);

		glUseProgram(program);
		const int dimensionsLocation = m_ShaderLibrary.Uniform("isosurface_compute", "u_Dimensions");
		const int cellLocation = m_ShaderLibrary.Uniform("isosurface_compute", "u_Cell");
		const int isoLocation = m_ShaderLibrary.Uniform("isosurface_compute", "u_IsoValue");
		const int signLocation = m_ShaderLibrary.Uniform("isosurface_compute", "u_LobeSign");
		const int maxVerticesLocation = m_ShaderLibrary.Uniform("isosurface_compute", "u_MaxVertices");
		if (dimensionsLocation >= 0)
			glUniform3i(dimensionsLocation, grid.dimensions.x, grid.dimensions.y, grid.dimensions.z);
		if (cellLocation >= 0)
			glUniformMatrix3fv(cellLocation, 1, GL_FALSE, &grid.cell[0][0]);
		if (isoLocation >= 0)
			glUniform1f(isoLocation, isoValue);
		if (maxVerticesLocation >= 0)
			glUniform1ui(maxVerticesLocation, static_cast<unsigned int>(kMaxIsosurfaceGpuVertices));

		const int cellsX = grid.dimensions.x - 1;
		const int cellsY = grid.dimensions.y - 1;
		const int cellsZ = grid.dimensions.z - 1;
		const std::uint32_t totalCells = static_cast<std::uint32_t>(cellsX) *
			static_cast<std::uint32_t>(cellsY) * static_cast<std::uint32_t>(cellsZ);
		const std::uint32_t groups = (totalCells + 63u) / 64u;

		for (const float lobeSign : {1.0f, -1.0f})
		{
			if (signLocation >= 0)
				glUniform1f(signLocation, lobeSign);
			glDispatchCompute(groups, 1, 1);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		}

		unsigned int vertexCount = 0;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.isosurfaceCounterSsbo[slot]);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(unsigned int), &vertexCount);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		vertexCount = std::min(vertexCount, static_cast<unsigned int>(kMaxIsosurfaceGpuVertices));
		DS_LOG_INFO("RegenerateIsosurfaceGpu: generated {} vertices", vertexCount);
		return static_cast<int>(vertexCount);
	}

	// T09 extension point: GPU-side bond transform via compute shader.
	// SSBO i shader są inicjalizowane, ale dispatch nie jest wywoływany.
	// Aktywować gdy T09 wprowadzi automatyczną regenerację bondów przy przesuwaniu atomów.
	void OpenGlRendererBackend::dispatchBondCompute(const RendererStructureData &structure)
	{
		const unsigned int program = m_ShaderLibrary.Program("bond_compute");
		if (program == 0 || structure.bonds.empty())
			return;

		std::vector<BondComputeInput> inputs;
		inputs.reserve(structure.bonds.size());
		for (const RendererBondData &bond : structure.bonds)
		{
			if (bond.firstAtomIndex >= structure.atoms.size() || bond.secondAtomIndex >= structure.atoms.size())
				continue;
			const RendererAtomData &a = structure.atoms[bond.firstAtomIndex];
			const RendererAtomData &b = structure.atoms[bond.secondAtomIndex];
			BondComputeInput input;
			input.start = glm::vec4(a.cartesianPosition, a.radius);
			input.finish = glm::vec4(b.cartesianPosition, b.radius);
			input.colorA = glm::vec4(bond.gradient.start, 1.0f);
			input.colorB = glm::vec4(bond.gradient.finish, 1.0f);
			input.radius = bond.radius;
			inputs.push_back(input);
		}
		if (inputs.empty())
			return;

		std::vector<BondComputeOutput> outputs(inputs.size());
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ComputeInputSsbo);
		const GLsizeiptr requiredInputBytes = static_cast<GLsizeiptr>(inputs.size() * sizeof(BondComputeInput));
		GLint currentInputSize = 0;
		glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentInputSize);
		if (static_cast<GLsizeiptr>(currentInputSize) < requiredInputBytes)
		{
			const GLsizeiptr newSize = requiredInputBytes + requiredInputBytes / 2;
			glBufferData(GL_SHADER_STORAGE_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
		}
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, requiredInputBytes, inputs.data());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ComputeInputSsbo);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ComputeOutputSsbo);
		const GLsizeiptr requiredOutputBytes = static_cast<GLsizeiptr>(outputs.size() * sizeof(BondComputeOutput));
		GLint currentOutputSize = 0;
		glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentOutputSize);
		if (static_cast<GLsizeiptr>(currentOutputSize) < requiredOutputBytes)
		{
			const GLsizeiptr newSize = requiredOutputBytes + requiredOutputBytes / 2;
			glBufferData(GL_SHADER_STORAGE_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
		}
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, requiredOutputBytes, outputs.data());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ComputeOutputSsbo);

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.BondCompute");
#endif

		glUseProgram(program);
		const int countLocation = m_ShaderLibrary.Uniform("bond_compute", "u_BondCount");
		if (countLocation >= 0)
			glUniform1i(countLocation, static_cast<int>(inputs.size()));
		const std::uint32_t groups = static_cast<std::uint32_t>((inputs.size() + 63) / 64);
		glDispatchCompute(groups, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}

	bool OpenGlRendererBackend::CaptureWindowToPng(
		const std::string &windowKey,
		const Path &outputPath,
		std::string &error,
		float cropLeft,
		float cropRight,
		float cropTop,
		float cropBottom) const
	{
		const auto it = m_Viewports.find(windowKey);
		if (it == m_Viewports.end())
		{
			error = "No rendered viewport found for window '" + windowKey + "'";
			return false;
		}

		const OpenGlFrameBuffer &frameBuffer = it->second.frameBuffer;
		const int fullWidth = frameBuffer.Width();
		const int fullHeight = frameBuffer.Height();
		if (fullWidth <= 0 || fullHeight <= 0)
		{
			error = "Viewport framebuffer has zero size";
			return false;
		}

		cropLeft = std::clamp(cropLeft, 0.0f, 0.9f);
		cropRight = std::clamp(cropRight, 0.0f, 0.9f - cropLeft);
		cropTop = std::clamp(cropTop, 0.0f, 0.9f);
		cropBottom = std::clamp(cropBottom, 0.0f, 0.9f - cropTop);

		const int leftPx = static_cast<int>(std::lround(cropLeft * fullWidth));
		const int rightPx = static_cast<int>(std::lround(cropRight * fullWidth));
		const int topPx = static_cast<int>(std::lround(cropTop * fullHeight));
		const int bottomPx = static_cast<int>(std::lround(cropBottom * fullHeight));
		const int width = std::max(1, fullWidth - leftPx - rightPx);
		const int height = std::max(1, fullHeight - topPx - bottomPx);

		// GL's y-origin is the bottom of the image, so trimming the image's top edge means
		// skipping rows at the HIGH end of GL's y range - i.e. starting the read at bottomPx.
		std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
		frameBuffer.Bind();
		glReadPixels(leftPx, bottomPx, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		frameBuffer.Unbind();

		// OpenGL's row 0 is the bottom of the image; PNG expects row 0 at the top.
		std::vector<unsigned char> flipped(pixels.size());
		const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
		for (int row = 0; row < height; ++row)
		{
			std::memcpy(
				flipped.data() + static_cast<std::size_t>(row) * rowBytes,
				pixels.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes,
				rowBytes);
		}

		FileSystem::CreateDirectories(outputPath.parent_path().Native());
		if (!stbi_write_png(outputPath.String().c_str(), width, height, 4, flipped.data(), static_cast<int>(rowBytes)))
		{
			error = "stbi_write_png failed for '" + outputPath.String() + "'";
			return false;
		}
		return true;
	}

	OpenGlViewportResources &OpenGlRendererBackend::viewportResources(const std::string &windowKey, int width, int height)
	{
		OpenGlViewportResources &resources = m_Viewports[windowKey];
		resources.frameBuffer.Resize(width, height);
		return resources;
	}

	glm::mat4 OpenGlRendererBackend::buildBondTransform(const glm::vec3 &start, const glm::vec3 &finish, float radius) const
	{
		if (!IsFiniteVec3(start) || !IsFiniteVec3(finish) || !std::isfinite(radius))
			return glm::mat4(1.0f);

		const glm::vec3 direction = finish - start;
		const float length = glm::length(direction);
		if (!std::isfinite(length) || length <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 zAxis = direction / length;
		glm::vec3 helperUp = glm::vec3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(zAxis, helperUp)) > 0.97f)
			helperUp = glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 xCandidate = glm::cross(helperUp, zAxis);
		const float xLength = glm::length(xCandidate);
		if (!std::isfinite(xLength) || xLength <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 xAxis = xCandidate / xLength;
		const glm::vec3 yCandidate = glm::cross(zAxis, xAxis);
		const float yLength = glm::length(yCandidate);
		if (!std::isfinite(yLength) || yLength <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 yAxis = yCandidate / yLength;
		if (!IsFiniteVec3(xAxis) || !IsFiniteVec3(yAxis) || !IsFiniteVec3(zAxis))
			return glm::mat4(1.0f);

		glm::mat4 rotation(1.0f);
		rotation[0] = glm::vec4(xAxis, 0.0f);
		rotation[1] = glm::vec4(yAxis, 0.0f);
		rotation[2] = glm::vec4(zAxis, 0.0f);

		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), start);
		const glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(radius, radius, length));
		return translation * rotation * scaling;
	}

	glm::mat4 OpenGlRendererBackend::buildArrowRevolutionTransform(const glm::vec3 &start, const glm::vec3 &end) const
	{
		if (!IsFiniteVec3(start) || !IsFiniteVec3(end))
			return glm::mat4(1.0f);

		const glm::vec3 direction = end - start;
		const float length = glm::length(direction);
		if (!std::isfinite(length) || length <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 zAxis = direction / length;
		glm::vec3 helperUp = glm::vec3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(zAxis, helperUp)) > 0.97f)
			helperUp = glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 xCandidate = glm::cross(helperUp, zAxis);
		const float xLength = glm::length(xCandidate);
		if (!std::isfinite(xLength) || xLength <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 xAxis = xCandidate / xLength;
		const glm::vec3 yCandidate = glm::cross(zAxis, xAxis);
		const float yLength = glm::length(yCandidate);
		if (!std::isfinite(yLength) || yLength <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 yAxis = yCandidate / yLength;
		if (!IsFiniteVec3(xAxis) || !IsFiniteVec3(yAxis) || !IsFiniteVec3(zAxis))
			return glm::mat4(1.0f);

		glm::mat4 rotation(1.0f);
		rotation[0] = glm::vec4(xAxis, 0.0f);
		rotation[1] = glm::vec4(yAxis, 0.0f);
		rotation[2] = glm::vec4(zAxis, 0.0f);

		return glm::translate(glm::mat4(1.0f), start) * rotation;
	}
} // namespace DefectStudio
