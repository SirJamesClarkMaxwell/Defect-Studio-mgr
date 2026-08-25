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

	constexpr glm::vec4 kLabelColor(0.92f, 0.92f, 0.85f, 1.0f);
	// Matches kSelectionHighlightColor used for atom selection below - same accent, reused here so
	// a selected pinned measurement reads as "selected" the same way a selected atom does.
	constexpr glm::vec4 kPinnedLabelSelectedColor(0.91f, 0.52f, 0.02f, 1.0f);

	// Shared by every label call site below - lays out one string's glyph quads (pen-advance +
	// centering) around `worldCenter` and appends them to whichever instance list the caller is
	// building.
	void AppendLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &worldCenter,
		const std::u32string &text,
		std::vector<OpenGlLabelInstance> &outInstances,
		const glm::vec4 &color = kLabelColor,
		float rotationRadians = 0.0f,
		float scale = 1.0f)
	{
		// World-space label height (em units -> world units) and a rough baseline centering
		// offset (typical glyph ascent/descent split) - tuned by eye, not derived from font
		// metrics, good enough for a fixed-purpose label rather than general text layout.
		constexpr float kWorldFontSize = 0.28f;
		constexpr float kBaselineOffset = -0.35f * kWorldFontSize;

		float totalAdvance = 0.0f;
		for (const char32_t codepoint : text)
			totalAdvance += font.GetGlyphQuad(codepoint).advance;

		float penX = -totalAdvance * 0.5f * kWorldFontSize;
		for (const char32_t codepoint : text)
		{
			const MsdfGlyphQuad glyph = font.GetGlyphQuad(codepoint);
			if (glyph.found && glyph.planeMax.x > glyph.planeMin.x && glyph.planeMax.y > glyph.planeMin.y)
			{
				OpenGlLabelInstance instance;
				instance.worldCenter = worldCenter;
				instance.localOffsetSize = scale * glm::vec4(
					penX + glyph.planeMin.x * kWorldFontSize,
					kBaselineOffset + glyph.planeMin.y * kWorldFontSize,
					(glyph.planeMax.x - glyph.planeMin.x) * kWorldFontSize,
					(glyph.planeMax.y - glyph.planeMin.y) * kWorldFontSize);
				instance.atlasUvMinMax = glm::vec4(glyph.atlasUvMin, glyph.atlasUvMax);
				instance.color = color;
				instance.rotationRadians = rotationRadians;
				outInstances.push_back(instance);
			}
			penX += glyph.advance * kWorldFontSize;
		}
	}

	void AppendBondLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &midpoint,
		float lengthAngstrom,
		std::vector<OpenGlLabelInstance> &outInstances,
		const glm::vec4 &color = kLabelColor,
		float rotationRadians = 0.0f,
		float scale = 1.0f)
	{
		AppendLabelInstances(font, midpoint, FormatBondLengthLabel(lengthAngstrom), outInstances, color, rotationRadians, scale);
	}

	void AppendAngleLabelInstances(
		const MsdfFont &font,
		const glm::vec3 &vertex,
		float angleDeg,
		std::vector<OpenGlLabelInstance> &outInstances,
		const glm::vec4 &color = kLabelColor,
		float rotationRadians = 0.0f,
		float scale = 1.0f)
	{
		AppendLabelInstances(font, vertex, FormatAngleLabel(angleDeg), outInstances, color, rotationRadians, scale);
	}

	[[nodiscard]] glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
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
		int selectedPinnedMeasurement,
		const std::vector<std::size_t> &selectedAtomIndices,
		const std::vector<std::size_t> &selectedBondIndices,
		const std::vector<IsosurfaceVertex> *debugIsosurfaceMesh,
		const RendererWindowState::OrbitalOverlayChannel *orbitalChannelUp,
		const RendererWindowState::OrbitalOverlayChannel *orbitalChannelDown)
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
			renderGrid(structure, camera, resources, globalSettings);
		if (showCellBox)
			renderCellBox(structure, camera, resources);
		if (showBonds)
			renderBonds(structure, camera, resources, globalSettings, selectedBondIndices);
		if (showLabels || !pinnedMeasurements.empty())
			renderLabels(structure, camera, resources, showLabels, pinnedMeasurements, selectedPinnedMeasurement);
		if (showAtoms)
			renderAtoms(structure, camera, resources, globalSettings, selectedAtomIndices);
		if (debugIsosurfaceMesh && !debugIsosurfaceMesh->empty())
			renderIsosurfaceOverlay(*debugIsosurfaceMesh, camera, globalSettings);
		if (orbitalChannelUp != nullptr && orbitalChannelUp->enabled && orbitalChannelUp->vertexCount > 0)
			renderIsosurfaceGpuOverlay(resources.isosurfaceVao[0], orbitalChannelUp->vertexCount, camera, globalSettings,
				orbitalChannelUp->positiveLobeColor, orbitalChannelUp->negativeLobeColor, orbitalChannelUp->lobeAlpha);
		if (orbitalChannelDown != nullptr && orbitalChannelDown->enabled && orbitalChannelDown->vertexCount > 0)
			renderIsosurfaceGpuOverlay(resources.isosurfaceVao[1], orbitalChannelDown->vertexCount, camera, globalSettings,
				orbitalChannelDown->positiveLobeColor, orbitalChannelDown->negativeLobeColor,
				orbitalChannelDown->lobeAlpha);

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

		createScreenGrid();
		createIsosurfaceGeometry();
		createLabelQuadMesh();
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
		}

		m_LabelFont.reset();

		const std::array<OpenGlMeshHandles *, 3> meshes = {&m_SphereMesh, &m_CylinderMesh, &m_LabelQuadMesh};
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

	void OpenGlRendererBackend::createLabelQuadMesh()
	{
		constexpr glm::vec2 kQuadVertices[4] = {
			glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f)};
		constexpr std::uint32_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

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

		glBindVertexArray(0);
		m_LabelQuadMesh.indexCount = 6;
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
		const std::vector<std::size_t> &selectedIndices)
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
		const std::vector<std::size_t> &selectedIndices)
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
				const float bondRadius = std::max(bond.radius, 0.001f);
				const glm::vec3 direction = axis / axisLength;
				const float rawShrinkA = std::sqrt(
					std::max(firstAtom.radius * firstAtom.radius - bondRadius * bondRadius, 0.0f));
				const float shrinkA = std::min(rawShrinkA, axisLength * 0.45f);
				const float rawShrinkB = std::sqrt(
					std::max(secondAtom.radius * secondAtom.radius - bondRadius * bondRadius, 0.0f));
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
		int selectedPinnedMeasurement)
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
		const std::vector<OpenGlLabelInstance> *instancesToDraw = nullptr;

		if (showAllLabels)
		{
			if (resources.labelsDirty)
			{
				resources.cachedLabelInstances.clear();
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
					AppendBondLabelInstances(*m_LabelFont, midpoint, lengthAngstrom, resources.cachedLabelInstances);
				}
				resources.labelsDirty = false;
			}
			instancesToDraw = &resources.cachedLabelInstances;
		}
		else if (!pinnedMeasurements.empty())
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
				const glm::vec4 &color =
					static_cast<int>(pinIndex) == selectedPinnedMeasurement ? kPinnedLabelSelectedColor : kLabelColor;
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
						AppendBondLabelInstances(
							*m_LabelFont, midpoint + offset, lengthAngstrom, pinnedInstances, color,
							rotationRadians + pin.rotationOffsetRadians, pin.scale);
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
						AppendAngleLabelInstances(
							*m_LabelFont, vertexAtom.cartesianPosition + offset, angleDeg, pinnedInstances, color,
							pin.rotationOffsetRadians, pin.scale);
					}
				}
			}
			instancesToDraw = &pinnedInstances;
		}

		if (instancesToDraw == nullptr || instancesToDraw->empty())
			return;

		const unsigned int program = m_ShaderLibrary.Program("labels");
		if (program == 0)
			return;

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

		const glm::mat4 view = camera.ViewMatrix();
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * view;
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
	}

	void OpenGlRendererBackend::renderCellBox(
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources)
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
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		if (colorLocation >= 0)
			glUniform4f(colorLocation, 0.85f, 0.85f, 0.9f, 1.0f);
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
		const RendererGlobalRenderSettings &globalSettings)
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
		if (viewProjectionLocation >= 0)
			glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
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
		float lobeAlpha)
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
} // namespace DefectStudio
