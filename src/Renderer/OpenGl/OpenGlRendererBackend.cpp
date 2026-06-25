#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glad/gl.h>

#include "Core/Utils/Logger.hpp"
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

		Result<void> computeLoaded = m_ShaderLibrary.LoadComputeProgram(
			"bond_compute",
			m_ShaderDirectory / Path("bond_transform.comp"));
		if (!computeLoaded.HasValue())
			return computeLoaded.Error();

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
		const std::vector<std::size_t> &selectedAtomIndices)
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
			renderBonds(structure, camera, resources, globalSettings);
		if (showAtoms)
			renderAtoms(structure, camera, resources, globalSettings, selectedAtomIndices);

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

		const std::array<OpenGlMeshHandles *, 2> meshes = {&m_SphereMesh, &m_CylinderMesh};
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
				const glm::vec3 displayColor = isSelected
					? glm::clamp(atom.color + glm::vec3(0.35f, 0.35f, 0.10f), glm::vec3(0.0f), glm::vec3(1.0f))
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
		const RendererGlobalRenderSettings &globalSettings)
	{
		if (resources.bondsDirty)
		{
			resources.cachedBondInstances.clear();
			resources.cachedBondInstances.reserve(structure.bonds.size());
			for (const RendererBondData &bond : structure.bonds)
			{
				if (bond.firstAtomIndex >= structure.atoms.size() || bond.secondAtomIndex >= structure.atoms.size())
					continue;
				const RendererAtomData &firstAtom = structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &secondAtom = structure.atoms[bond.secondAtomIndex];
				if (!firstAtom.visible || !secondAtom.visible)
					continue;
				const glm::vec3 axis = secondAtom.cartesianPosition - firstAtom.cartesianPosition;
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
				const glm::vec3 bondEnd = secondAtom.cartesianPosition - direction * shrinkB;
				OpenGlBondInstance instance;
				instance.model = buildBondTransform(bondStart, bondEnd, bondRadius);
				instance.colorA = glm::vec4(bond.gradient.start, 1.0f);
				instance.colorB = glm::vec4(bond.gradient.finish, 1.0f);
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
		glLineWidth(1.4f);

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
