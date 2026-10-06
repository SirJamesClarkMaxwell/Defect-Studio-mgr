#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "Renderer/Scene/IsosurfaceMesher.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio::Tests
{
	TEST(Task78OrbitalNormalsTests, InterpolatedGradientIsRadialAndVariesWithinEachFace)
	{
		OrbitalGridData grid;
		grid.dimensions = glm::ivec3(9);
		grid.cell = glm::mat3(1.0f);
		grid.values.resize(9u * 9u * 9u);
		for (int x = 0; x < 9; ++x)
			for (int y = 0; y < 9; ++y)
				for (int z = 0; z < 9; ++z)
				{
					const glm::vec3 offset = glm::vec3(x,y,z) - glm::vec3(4.0f);
					grid.values[static_cast<std::size_t>(x)*81 + static_cast<std::size_t>(y)*9 + z] =
						9.0f - glm::dot(offset, offset);
				}
		const auto mesh = GenerateIsosurfaceMesh(grid, 3.0f);
		ASSERT_FALSE(mesh.empty());
		bool varying = false;
		const glm::vec3 centre(4.0f / 9.0f);
		for (const auto &vertex : mesh)
		{
			EXPECT_TRUE(std::isfinite(vertex.normal.x));
			EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-5f);
			if (vertex.sign > 0.0f)
				EXPECT_GT(glm::dot(vertex.normal, glm::normalize(vertex.position - centre)), 0.999f);
		}
		for (std::size_t i = 0; i + 2 < mesh.size(); i += 3)
			if (mesh[i].sign > 0.0f && glm::length(mesh[i].normal - mesh[i+1].normal) > 0.01f)
				varying = true;
		EXPECT_TRUE(varying); // Flat face normals would fail this, even at this coarse resolution.
	}

	TEST(Task78OrbitalNormalsTests, SceneAndLcaoMeshesBothUseSmoothNormals)
	{
		RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital;
		orbital.preset = OrbitalPreset::S;
		orbital.resolution = 12;
		for (int lcao = 0; lcao < 2; ++lcao)
		{
			if (lcao)
			{
				RendererWindowState::SceneOrbital::LcaoComponent component;
				component.preset = OrbitalPreset::S;
				component.shell = 1;
				orbital.lcaoComponents = {component};
			}
			const auto mesh = BuildSceneOrbitalMesh(orbital, structure);
			ASSERT_FALSE(mesh.empty());
			bool smooth = false;
			for (std::size_t i = 0; i + 2 < mesh.size(); i += 3)
				smooth |= glm::length(mesh[i].normal - mesh[i+1].normal) > 0.01f;
			EXPECT_TRUE(smooth);
		}
	}

	TEST(Task81OrbitalNormalsTests, ExactFieldGivesRadialNormalsAndFlatModeGivesFaceNormals)
	{
		// Endpoint-inclusive sampling of a sphere, like the analytic orbital sampler.
		constexpr int n = 13;
		const glm::vec3 centre(0.5f);
		const auto field = [centre](const glm::vec3 &p) { return 0.16f - glm::dot(p - centre, p - centre); };
		OrbitalGridData grid;
		grid.dimensions = glm::ivec3(n);
		grid.cell = glm::mat3(1.0f);
		grid.values.resize(static_cast<std::size_t>(n * n * n));
		for (int x = 0; x < n; ++x)
			for (int y = 0; y < n; ++y)
				for (int z = 0; z < n; ++z)
					grid.values[(static_cast<std::size_t>(x) * n + y) * n + z] =
						field(glm::vec3(x, y, z) / static_cast<float>(n - 1));
		IsosurfaceMeshOptions options;
		options.endpointInclusive = true;
		options.field = field;
		const auto smooth = GenerateIsosurfaceMesh(grid, 0.07f, options);
		ASSERT_FALSE(smooth.empty());
		for (const auto &vertex : smooth)
			if (vertex.sign > 0.0f)
			{
				// Crossings sit on the true surface and normals follow the exact gradient.
				EXPECT_NEAR(glm::length(vertex.position - centre), 0.3f, 1e-3f);
				EXPECT_GT(glm::dot(vertex.normal, glm::normalize(vertex.position - centre)), 0.9995f);
			}
		options.smoothShading = false;
		const auto flat = GenerateIsosurfaceMesh(grid, 0.07f, options);
		ASSERT_EQ(flat.size(), smooth.size());
		for (std::size_t i = 0; i + 2 < flat.size(); i += 3)
		{
			EXPECT_EQ(flat[i].normal, flat[i + 1].normal);
			EXPECT_EQ(flat[i].normal, flat[i + 2].normal);
		}
	}

	TEST(Task81OrbitalNormalsTests, SceneOrbitalFlagSwitchesBetweenSmoothAndFlat)
	{
		RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital;
		orbital.preset = OrbitalPreset::P;
		orbital.resolution = 16;
		orbital.smoothShading = false;
		const auto mesh = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_FALSE(mesh.empty());
		for (std::size_t i = 0; i + 2 < mesh.size(); i += 3)
			EXPECT_LT(glm::length(mesh[i].normal - mesh[i + 1].normal), 1e-4f);
		EXPECT_NE(MakeSceneOrbitalMeshKey(orbital, structure).hash,
			[&] { auto copy = orbital; copy.smoothShading = true; return MakeSceneOrbitalMeshKey(copy, structure).hash; }());
	}

	TEST(Task78OrbitalPickingTests, SurfaceInFrontOfAnAtomIsHitAndEmptyBoundingSphereSpaceMisses)
	{
		RendererWindowState window;
		RendererWindowState::SceneOrbital orbital;
		orbital.id = window.sceneRegistry.AllocateObjectId();
		orbital.preset = OrbitalPreset::S;
		orbital.resolution = 16;
		window.sceneOrbitals = {orbital};
		window.structure.atoms.resize(1);
		window.structure.atoms[0].radius = 0.1f;
		const auto bounds = SceneOrbitalWorldBounds(orbital, window.structure);
		const glm::vec3 origin(0,0,bounds.radius * 4.0f);
		const auto hit = PickSceneOrbitalSurface(window, window.structure, origin, glm::vec3(0,0,-1));
		ASSERT_TRUE(hit);
		EXPECT_LT(hit->distance, origin.z - window.structure.atoms[0].radius);
		EXPECT_FALSE(PickSceneOrbital(window, window.structure,
			origin + glm::vec3(bounds.radius * 0.95f,0,0), glm::vec3(0,0,-1)));
		window.sceneOrbitals[0].visible = false;
		EXPECT_FALSE(PickSceneOrbital(window, window.structure, origin, glm::vec3(0,0,-1)));
	}
}
