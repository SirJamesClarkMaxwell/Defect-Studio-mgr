#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <array>
#include <cmath>

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
		glm::vec4 radiusAndPadding = glm::vec4(0.0f);
	};

	struct BondComputeOutput
	{
		glm::mat4 transform = glm::mat4(1.0f);
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

		const bool isKnownPerformanceSpam = type == GL_DEBUG_TYPE_PERFORMANCE && id == 131185u;
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

	OpenGlRendererBackend::~OpenGlRendererBackend()
	{
		Shutdown();
	}

	Result<void> OpenGlRendererBackend::Initialize(const Path &shaderDirectory)
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

		createStaticGeometry();

		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(OpenGlDebugCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
		const std::array<unsigned int, 1> filteredMessageIds = {131185u};
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

	unsigned int OpenGlRendererBackend::RenderWindow(
		const std::string &windowKey,
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		int viewportWidth,
		int viewportHeight,
		bool showAtoms,
		bool showBonds,
		bool showCellBox,
		bool showGrid)
	{
		if (!m_Initialized)
			return 0;

		OpenGlViewportResources &resources = viewportResources(windowKey, viewportWidth, viewportHeight);
		resources.frameBuffer.Bind();
		glViewport(0, 0, resources.frameBuffer.Width(), resources.frameBuffer.Height());
		glClearColor(0.06f, 0.07f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		configureOpenGlState();

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Pass");
#endif

		if (showGrid)
			renderGrid(camera);
		if (showCellBox)
			renderCellBox(structure, camera);
		if (showBonds)
			renderBonds(structure, camera);
		if (showAtoms)
			renderAtoms(structure, camera);

		resources.frameBuffer.Unbind();
		resources.lastRenderTime = Time::NowSteady();
		return resources.frameBuffer.ColorTextureId();
	}

	void OpenGlRendererBackend::createStaticGeometry()
	{
		createSphereMesh();
		createCylinderMesh();
		createScreenGrid();
		glGenBuffers(1, &m_ComputeInputSsbo);
		glGenBuffers(1, &m_ComputeOutputSsbo);
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

	void OpenGlRendererBackend::createSphereMesh()
	{
		const int latitudeSegments = 14;
		const int longitudeSegments = 18;

		std::vector<SphereVertex> vertices;
		std::vector<std::uint32_t> indices;
		for (int lat = 0; lat <= latitudeSegments; ++lat)
		{
			const float v = static_cast<float>(lat) / static_cast<float>(latitudeSegments);
			const float theta = v * 3.1415926535f;
			const float y = std::cos(theta);
			const float ringRadius = std::sin(theta);
			for (int lon = 0; lon <= longitudeSegments; ++lon)
			{
				const float u = static_cast<float>(lon) / static_cast<float>(longitudeSegments);
				const float phi = u * 6.283185307f;
				const float x = ringRadius * std::cos(phi);
				const float z = ringRadius * std::sin(phi);
				SphereVertex vertex;
				vertex.position = glm::vec3(x, y, z);
				vertex.normal = glm::normalize(vertex.position);
				vertices.push_back(vertex);
			}
		}

		for (int lat = 0; lat < latitudeSegments; ++lat)
		{
			for (int lon = 0; lon < longitudeSegments; ++lon)
			{
				const std::uint32_t rowStart = static_cast<std::uint32_t>(lat * (longitudeSegments + 1));
				const std::uint32_t nextRowStart = static_cast<std::uint32_t>((lat + 1) * (longitudeSegments + 1));
				const std::uint32_t current = rowStart + static_cast<std::uint32_t>(lon);
				const std::uint32_t next = current + 1;
				const std::uint32_t below = nextRowStart + static_cast<std::uint32_t>(lon);
				const std::uint32_t belowNext = below + 1;
				indices.insert(indices.end(), {current, below, next, next, below, belowNext});
			}
		}

		glGenVertexArrays(1, &m_SphereMesh.vao);
		glGenBuffers(1, &m_SphereMesh.vbo);
		glGenBuffers(1, &m_SphereMesh.ebo);
		glGenBuffers(1, &m_SphereMesh.instanceVbo);
		glBindVertexArray(m_SphereMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_SphereMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(SphereVertex)), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SphereMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);

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
		m_SphereMesh.indexCount = static_cast<int>(indices.size());
	}

	void OpenGlRendererBackend::createCylinderMesh()
	{
		const int segments = 8;
		std::vector<CylinderVertex> vertices;
		std::vector<std::uint32_t> indices;
		for (int ring = 0; ring <= 1; ++ring)
		{
			const float z = static_cast<float>(ring);
			for (int segment = 0; segment < segments; ++segment)
			{
				const float angle = 6.283185307f * static_cast<float>(segment) / static_cast<float>(segments);
				const float x = std::cos(angle);
				const float y = std::sin(angle);
				CylinderVertex vertex;
				vertex.position = glm::vec3(x, y, z);
				vertex.normal = glm::vec3(x, y, 0.0f);
				vertex.gradientT = z;
				vertices.push_back(vertex);
			}
		}

		for (int segment = 0; segment < segments; ++segment)
		{
			const int next = (segment + 1) % segments;
			const std::uint32_t a = static_cast<std::uint32_t>(segment);
			const std::uint32_t b = static_cast<std::uint32_t>(next);
			const std::uint32_t c = static_cast<std::uint32_t>(segment + segments);
			const std::uint32_t d = static_cast<std::uint32_t>(next + segments);
			indices.insert(indices.end(), {a, c, b, b, c, d});
		}

		glGenVertexArrays(1, &m_CylinderMesh.vao);
		glGenBuffers(1, &m_CylinderMesh.vbo);
		glGenBuffers(1, &m_CylinderMesh.ebo);
		glGenBuffers(1, &m_CylinderMesh.instanceVbo);
		glBindVertexArray(m_CylinderMesh.vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(CylinderVertex)), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_CylinderMesh.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);

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
		m_CylinderMesh.indexCount = static_cast<int>(indices.size());
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

	void OpenGlRendererBackend::renderAtoms(const RendererStructureData &structure, const RendererViewCamera &camera)
	{
		std::vector<OpenGlAtomInstance> instances;
		instances.reserve(structure.atoms.size());
		for (const RendererAtomData &atom : structure.atoms)
		{
			if (!atom.visible)
				continue;
			OpenGlAtomInstance instance;
			instance.positionRadius = glm::vec4(atom.cartesianPosition, atom.radius);
			instance.color = glm::vec4(atom.color, 1.0f);
			instances.push_back(instance);
		}
		if (instances.empty())
			return;

		const unsigned int program = m_ShaderLibrary.Program("atoms");
		if (program == 0)
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Atoms");
#endif

		glBindBuffer(GL_ARRAY_BUFFER, m_SphereMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(instances.size() * sizeof(OpenGlAtomInstance)), instances.data(), GL_DYNAMIC_DRAW);

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = glGetUniformLocation(program, "u_ViewProjection");
		glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);

		glBindVertexArray(m_SphereMesh.vao);
		glDrawElementsInstanced(GL_TRIANGLES, m_SphereMesh.indexCount, GL_UNSIGNED_INT, nullptr, static_cast<int>(instances.size()));
		glBindVertexArray(0);
	}

	void OpenGlRendererBackend::renderBonds(const RendererStructureData &structure, const RendererViewCamera &camera)
	{
		std::vector<OpenGlBondInstance> instances;
		instances.reserve(structure.bonds.size());
		for (const RendererBondData &bond : structure.bonds)
		{
			if (bond.firstAtomIndex >= structure.atoms.size() || bond.secondAtomIndex >= structure.atoms.size())
				continue;
			const RendererAtomData &firstAtom = structure.atoms[bond.firstAtomIndex];
			const RendererAtomData &secondAtom = structure.atoms[bond.secondAtomIndex];
			if (!firstAtom.visible || !secondAtom.visible)
				continue;

			OpenGlBondInstance instance;
			instance.model = buildBondTransform(firstAtom.cartesianPosition, secondAtom.cartesianPosition, bond.radius);
			instance.colorA = glm::vec4(bond.gradient.start, 1.0f);
			instance.colorB = glm::vec4(bond.gradient.finish, 1.0f);
			instances.push_back(instance);
		}
		if (instances.empty())
			return;

		dispatchBondCompute(structure);

		const unsigned int program = m_ShaderLibrary.Program("bonds");
		if (program == 0)
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.Bonds");
#endif

		glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.instanceVbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(instances.size() * sizeof(OpenGlBondInstance)), instances.data(), GL_DYNAMIC_DRAW);

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = glGetUniformLocation(program, "u_ViewProjection");
		glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);

		glBindVertexArray(m_CylinderMesh.vao);
		glDrawElementsInstanced(GL_TRIANGLES, m_CylinderMesh.indexCount, GL_UNSIGNED_INT, nullptr, static_cast<int>(instances.size()));
		glBindVertexArray(0);
	}

	void OpenGlRendererBackend::renderCellBox(const RendererStructureData &structure, const RendererViewCamera &camera)
	{
		if (structure.cellEdges.empty())
			return;

		std::vector<glm::vec3> vertices;
		vertices.reserve(structure.cellEdges.size() * 2);
		for (const RendererCellEdge &edge : structure.cellEdges)
		{
			vertices.push_back(edge.start);
			vertices.push_back(edge.finish);
		}

		const unsigned int program = m_ShaderLibrary.Program("lines");
		if (program == 0)
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.CellBox");
#endif

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = glGetUniformLocation(program, "u_ViewProjection");
		const int colorLocation = glGetUniformLocation(program, "u_LineColor");
		glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		glUniform4f(colorLocation, 0.85f, 0.85f, 0.9f, 1.0f);
		glLineWidth(1.4f);

		glBindVertexArray(m_LineVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(glm::vec3)), vertices.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_LINES, 0, static_cast<int>(vertices.size()));
		glBindVertexArray(0);
	}

	void OpenGlRendererBackend::renderGrid(const RendererViewCamera &camera)
	{
		std::vector<glm::vec3> gridVertices;
		const int halfLines = 20;
		const float spacing = 0.7f;
		const float halfSpan = static_cast<float>(halfLines) * spacing;
		const glm::vec3 center = camera.Target();
		for (int line = -halfLines; line <= halfLines; ++line)
		{
			const float coordinate = static_cast<float>(line) * spacing;
			gridVertices.push_back(glm::vec3(center.x - halfSpan, center.y, center.z + coordinate));
			gridVertices.push_back(glm::vec3(center.x + halfSpan, center.y, center.z + coordinate));
			gridVertices.push_back(glm::vec3(center.x + coordinate, center.y, center.z - halfSpan));
			gridVertices.push_back(glm::vec3(center.x + coordinate, center.y, center.z + halfSpan));
		}

		const unsigned int program = m_ShaderLibrary.Program("grid");
		if (program == 0)
			return;

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		glUseProgram(program);
		const int viewProjectionLocation = glGetUniformLocation(program, "u_ViewProjection");
		glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
		glBindVertexArray(m_LineVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(gridVertices.size() * sizeof(glm::vec3)), gridVertices.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_LINES, 0, static_cast<int>(gridVertices.size()));
		glBindVertexArray(0);
	}

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
			input.start = glm::vec4(a.cartesianPosition, 1.0f);
			input.finish = glm::vec4(b.cartesianPosition, 1.0f);
			input.radiusAndPadding = glm::vec4(bond.radius, 0.0f, 0.0f, 0.0f);
			inputs.push_back(input);
		}
		if (inputs.empty())
			return;

		std::vector<BondComputeOutput> outputs(inputs.size());
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ComputeInputSsbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<long long>(inputs.size() * sizeof(BondComputeInput)), inputs.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ComputeInputSsbo);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ComputeOutputSsbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<long long>(outputs.size() * sizeof(BondComputeOutput)), outputs.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ComputeOutputSsbo);

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.BondCompute");
#endif

		glUseProgram(program);
		const int countLocation = glGetUniformLocation(program, "u_BondCount");
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
		const glm::vec3 direction = finish - start;
		const float length = glm::length(direction);
		if (length <= 0.00001f)
			return glm::mat4(1.0f);
		const glm::vec3 zAxis = direction / length;
		glm::vec3 helperUp = glm::vec3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(zAxis, helperUp)) > 0.97f)
			helperUp = glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 xAxis = glm::normalize(glm::cross(helperUp, zAxis));
		const glm::vec3 yAxis = glm::normalize(glm::cross(zAxis, xAxis));

		glm::mat4 rotation(1.0f);
		rotation[0] = glm::vec4(xAxis, 0.0f);
		rotation[1] = glm::vec4(yAxis, 0.0f);
		rotation[2] = glm::vec4(zAxis, 0.0f);

		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), start);
		const glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(radius, radius, length));
		return translation * rotation * scaling;
	}
} // namespace DefectStudio
