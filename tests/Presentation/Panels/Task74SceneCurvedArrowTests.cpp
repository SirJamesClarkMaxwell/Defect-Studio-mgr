#include "Core/dspch.hpp"

#include <cmath>
#include <numbers>
#include <gtest/gtest.h>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void PrepareEnds(RendererWindowState &window, bool cycle)
		{
			window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}};
			if (cycle) window.structure.atoms.push_back({"C", {-1, -std::sqrt(3.0f), 0}});
			for (auto &atom : window.structure.atoms) atom.radius = 0.4f;
			window.selectedAtomIndices = cycle ? std::vector<std::size_t>{0, 1, 2} :
				std::vector<std::size_t>{0, 1};
			window.structure.defectFrame.emplace();
			window.structure.defectFrame->z = {0, 0, 1};
		}
	}

	TEST(Task74SceneCurvedArrowTests, ScaleOnePreservesLegacyBufferedGeometry)
	{
		for (bool cycle : {false, true})
		{
			RendererWindowState window;
			PrepareEnds(window, cycle);
			const auto added = AddCurvedArrowThroughSelectedAtoms(window,
				{.axisMode = CurvedArrowAxisMode::DefectZ, .radiusScale = 1.0f});
			ASSERT_TRUE(added);
			for (const auto id : *added)
			{
				const auto &path = *window.paths->Store().Find(id);
				const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
				for (std::size_t i = 0; i < 2; ++i)
				{
					const auto &binding = std::get<PathBinding::CopyPosition>(path.nodes[i].binding.value);
					const auto &other = std::get<PathBinding::CopyPosition>(path.nodes[1-i].binding.value);
					const auto &atom = window.structure.atoms[binding.atomIndex];
					const auto direction = glm::normalize(window.structure.atoms[other.atomIndex].cartesianPosition -
						atom.cartesianPosition);
					EXPECT_EQ(binding.offset, glm::vec3(0));
					EXPECT_FLOAT_EQ(binding.buffer, GetScenePathAtomBuffer());
					EXPECT_LT(glm::distance(resolved.positions[i],
						atom.cartesianPosition + direction * atom.radius * GetScenePathAtomBuffer()), 1.0e-5f);
				}
				const auto midpoint = EvaluateSegment(path, resolved, 0, 0.5);
				ASSERT_TRUE(midpoint);
				const glm::dvec3 chordCenter = (glm::dvec3(resolved.positions[0]) + glm::dvec3(resolved.positions[1])) * 0.5;
				const double sagitta = glm::distance(glm::dvec3(resolved.positions[0]), glm::dvec3(resolved.positions[1])) *
					0.5 * std::tan(std::numbers::pi_v<double> / 12);
				EXPECT_NEAR(glm::distance(midpoint->position, chordCenter), sagitta, 1.0e-5);
			}
		}
	}

	TEST(Task74SceneCurvedArrowTests, LargerScaleMovesEveryMidpointOutwardAndRetainsBindings)
	{
		for (bool cycle : {false, true})
		{
			RendererWindowState before, after;
			PrepareEnds(before, cycle);
			PrepareEnds(after, cycle);
			const auto oldIds = AddCurvedArrowThroughSelectedAtoms(before, {.axisMode = CurvedArrowAxisMode::DefectZ, .arrowCount = 1});
			const auto newIds = AddCurvedArrowThroughSelectedAtoms(after,
				{.axisMode = CurvedArrowAxisMode::DefectZ, .arrowCount = 1, .radiusScale = 1.3f});
			ASSERT_TRUE(oldIds);
			ASSERT_TRUE(newIds);
			ASSERT_EQ(oldIds->size(), newIds->size());
			for (std::size_t i = 0; i < oldIds->size(); ++i)
			{
				const auto &oldPath = *before.paths->Store().Find((*oldIds)[i]);
				const auto &newPath = *after.paths->Store().Find((*newIds)[i]);
				const auto oldSample = EvaluateSegment(oldPath,
					ResolveNodePositions(oldPath, SceneSystem::MakePathBindingContext(before)), 0, 0.5);
				const auto newSample = EvaluateSegment(newPath,
					ResolveNodePositions(newPath, SceneSystem::MakePathBindingContext(after)), 0, 0.5);
				ASSERT_TRUE(oldSample);
				ASSERT_TRUE(newSample);
				EXPECT_GT(glm::length(newSample->position), glm::length(oldSample->position));
				for (const auto &node : newPath.nodes)
					EXPECT_EQ(std::get<PathBinding::CopyPosition>(node.binding.value).offset, glm::vec3(0));
				const auto oldNodes = ResolveNodePositions(newPath, SceneSystem::MakePathBindingContext(after));
				const auto index = std::get<PathBinding::CopyPosition>(newPath.nodes[0].binding.value).atomIndex;
				after.structure.atoms[index].cartesianPosition.z += 1.0f;
				const auto moved = ResolveNodePositions(newPath, SceneSystem::MakePathBindingContext(after));
				EXPECT_GT(moved.positions.front().z, oldNodes.positions.front().z + 0.5f);
				after.structure.atoms[index].cartesianPosition.z -= 1.0f;
			}
		}
	}

	TEST(Task74SceneCurvedArrowTests, GapMeasuresSurfaceClearanceAtBothEndsIndependentOfSmallRadialOffsets)
	{
		for (bool cycle : {false, true})
			for (float scale : {1.0f, 1.05f})
				for (float gap : {0.0f, 0.15f, 0.5f})
				{
					RendererWindowState window;
					PrepareEnds(window, cycle);
					const auto added = AddCurvedArrowThroughSelectedAtoms(window,
						{.axisMode = CurvedArrowAxisMode::DefectZ, .radiusScale = scale, .endGap = gap});
					ASSERT_TRUE(added);
					for (const auto id : *added)
					{
						const auto &path = *window.paths->Store().Find(id);
						const auto nodes = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
						for (std::size_t i = 0; i < 2; ++i)
						{
							const auto atomIndex = std::get<PathBinding::CopyPosition>(path.nodes[i].binding.value).atomIndex;
							const auto &atom = window.structure.atoms[atomIndex];
							EXPECT_NEAR(glm::distance(nodes.positions[i], atom.cartesianPosition) - atom.radius,
								gap * atom.radius, 1.0e-5f);
						}
					}
				}
	}

	TEST(Task74SceneCurvedArrowTests, BondArcsAreEvenlySpacedClampedSelectedAndBound)
	{
		for (int count : {1, 2, 3, 6})
		{
			RendererWindowState window;
			window.structure.atoms = {{"C", {-0.77f, 0, 0}}, {"C", {0.77f, 0, 0}}};
			for (auto &atom : window.structure.atoms) atom.radius = 0.45f;
			window.selectedAtomIndices = {0, 1};
			const auto added = AddCurvedArrowThroughSelectedAtoms(window,
				{.arrowCount = count, .sweepDegrees = 400.0f, .rotationDegrees = 30.0f});
			ASSERT_TRUE(added);
			ASSERT_EQ(added->size(), static_cast<std::size_t>(count));
			EXPECT_EQ(window.selectedScenePaths, *added);
			for (int i = 0; i < count; ++i)
			{
				const auto &path = *window.paths->Store().Find((*added)[i]);
				const auto &arc = std::get<CircularArcSegmentData>(path.segments.front().data);
				const auto &binding = std::get<PathTransformBinding::BondFrame>(path.transformBinding.value);
				EXPECT_NEAR(arc.signedSweepRadians, glm::radians(360.0f / count - 5.0f), 1.0e-5f);
				EXPECT_NEAR(binding.rollRadians, glm::radians(30.0f + i * 360.0f / count), 1.0e-5f);
				EXPECT_EQ(binding.atomA, 0u);
				EXPECT_EQ(binding.atomB, 1u);
				const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
				const auto sample = EvaluateSegment(path, resolved, 0, 0.5);
				ASSERT_TRUE(sample);
				for (const auto &atom : window.structure.atoms)
					EXPECT_GT(glm::distance(sample->position, glm::dvec3(atom.cartesianPosition)),
						atom.radius + path.style.width * 0.5f);
			}
		}
	}
}