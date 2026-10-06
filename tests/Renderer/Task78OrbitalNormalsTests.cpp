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
