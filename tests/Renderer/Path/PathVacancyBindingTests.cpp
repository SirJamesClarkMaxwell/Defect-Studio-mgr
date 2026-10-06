#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "IO/SceneObjectsIO.hpp"
#include "Presentation/Panels/ScenePathBindingOperations.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ScenePathPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath Line()
		{
			ScenePath path;
			path.nodes = {{PathElementId{1}, glm::vec3(0), {}}, {PathElementId{2}, glm::vec3(10, 0, 0), {}}};
			path.segments = {{PathElementId{3}, LineSegmentData{}}};
			path.nextElementId = 4;
			return path;
		}

		BindingContext Vacancies()
		{
			BindingContext context;
			context.vacancyPosition = [](std::size_t index) -> std::optional<glm::vec3> {
				return index < 2 ? std::optional(glm::vec3(static_cast<float>(index) * 10, 0, 0)) : std::nullopt;
			};
			context.vacancyRadius = [](std::size_t index) -> std::optional<float> {
				return index < 2 ? std::optional(2.0f) : std::nullopt;
			};
			return context;
		}
	}

	TEST(PathVacancyBindingTests, ResolvesVacancyWithOffsetInWorldSpace)
	{
		ScenePath path = Line();
		path.transform.position = glm::vec3(100);
		path.transform.scale = glm::vec3(3);
		path.nodes[0].binding.value = PathBinding::CopyVacancy{1, {1, 2, 3}, 0};
		const auto resolved = ResolveNodePositions(path, Vacancies());
		EXPECT_EQ(resolved.positions[0], glm::vec3(11, 2, 3));
		EXPECT_TRUE(resolved.diagnostics.empty());
		EXPECT_EQ(path.nodes[0].position, glm::vec3(0));
	}

	TEST(PathVacancyBindingTests, BufferOneEndsOnMarkerEdgeAtBothEnds)
	{
		ScenePath path = Line();
		path.nodes[0].binding.value = PathBinding::CopyVacancy{0, {}, 1};
		path.nodes[1].binding.value = PathBinding::CopyVacancy{1, {}, 1};
		const auto resolved = ResolveNodePositions(path, Vacancies());
		EXPECT_EQ(resolved.positions[0], glm::vec3(2, 0, 0));
		EXPECT_EQ(resolved.positions[1], glm::vec3(8, 0, 0));
		EXPECT_TRUE(resolved.diagnostics.empty());
	}

	TEST(PathVacancyBindingTests, BufferUsesUnbufferedNeighbourAndClamps)
	{
		ScenePath path = Line();
		path.nodes[0].binding.value = PathBinding::CopyVacancy{0, {}, 4};
		path.nodes[1].binding.value = PathBinding::CopyVacancy{1, {}, 100};
		const auto resolved = ResolveNodePositions(path, Vacancies());
		EXPECT_FLOAT_EQ(resolved.positions[0].x, 8);
		// The second endpoint retains ten percent of the remaining gap; the ends cannot invert.
		EXPECT_NEAR(resolved.positions[1].x, 8.2f, 1e-5f);
		EXPECT_LT(resolved.positions[0].x, resolved.positions[1].x);
	}

	TEST(PathVacancyBindingTests, StaleVacancyFallsBackAndReportsBrokenBinding)
	{
		ScenePath path = Line();
		path.transform.position = glm::vec3(3, 4, 5);
		path.nodes[0].binding.value = PathBinding::CopyVacancy{99, {7, 8, 9}, 1};
		BindingContext context = Vacancies();
		// A radius alone must never buffer a broken position binding.
		context.vacancyRadius = [](std::size_t) -> std::optional<float> { return 2.0f; };
		const auto resolved = ResolveNodePositions(path, context);
		EXPECT_EQ(resolved.positions[0], glm::vec3(3, 4, 5));
		ASSERT_EQ(resolved.diagnostics.size(), 1u);
		EXPECT_EQ(resolved.diagnostics[0].code, PathDiagnosticCode::BrokenBinding);
		EXPECT_EQ(resolved.diagnostics[0].element, path.nodes[0].id);
		EXPECT_EQ(path.nodes[0].position, glm::vec3(0));
	}

	TEST(PathVacancyBindingTests, InteriorBufferReportsDiagnostic)
	{
		ScenePath path = Line();
		path.nodes.insert(path.nodes.begin() + 1, {PathElementId{4}, glm::vec3(5, 0, 0),
			PathBinding{PathBinding::CopyVacancy{0, {}, 1}}});
		path.segments.push_back({PathElementId{5}, LineSegmentData{}});
		const auto resolved = ResolveNodePositions(path, Vacancies());
		EXPECT_EQ(resolved.positions[1], glm::vec3(0));
		ASSERT_EQ(resolved.diagnostics.size(), 1u);
		EXPECT_EQ(resolved.diagnostics[0].code, PathDiagnosticCode::InteriorNodeBuffer);
	}

	TEST(PathVacancyBindingTests, MovingStructureVacancyChangesResolvedLineAndCacheRevision)
	{
		RendererWindowState window;
		window.structure.vacancies.resize(1);
		window.structure.vacancies[0].radius = 2;
		ScenePath path = Line();
		path.nodes[0].binding.value = PathBinding::CopyVacancy{0, {}, 1};
		const BindingContext context = SceneSystem::MakePathBindingContext(window);
		const auto before = ResolveNodePositions(path, context);
		EXPECT_EQ(before.positions[0], glm::vec3(2, 0, 0));
		window.structure.vacancies[0].cartesianPosition = glm::vec3(3, 0, 0);
		const auto after = ResolveNodePositions(path, context);
		EXPECT_EQ(after.positions[0], glm::vec3(5, 0, 0));
		EXPECT_EQ(after.positions[1], before.positions[1]);
		EXPECT_NE(BindingSourceRevision(path, before), BindingSourceRevision(path, after));
		EXPECT_EQ(BindingSourceRevision(path, after), BindingSourceRevision(path, ResolveNodePositions(path, context)));
	}

	TEST(PathVacancyBindingTests, VacancyBindingRoundTripsThroughYamlAndScenePersistence)
	{
		ScenePath path = Line();
		path.nodes[1].binding.value = PathBinding::CopyVacancy{1, {1, 2, 3}, 1.25f};
		RendererStructureData structure;
		structure.vacancies.resize(2);
		const auto saved = ExtractPersistedScenePath(path, structure);
		EXPECT_EQ(saved.nodes[1].binding.kind, "CopyVacancy");
		EXPECT_EQ(saved.nodes[1].binding.vacancyIndex, 1u);
		SceneObjectsFile file;
		file.structures.push_back({"k", {saved}});
		const std::string yaml = SceneObjectsIO::Serialize(file);
		EXPECT_NE(yaml.find("CopyVacancy"), std::string::npos);
		EXPECT_NE(yaml.find("vacancyIndex"), std::string::npos);
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(yaml, loaded, warnings, error)) << error;
		ASSERT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto &dto = std::get<PersistedScenePath>(loaded.structures[0].objects[0]);
		const auto built = BuildScenePath(dto, structure, warnings);
		ASSERT_TRUE(built);
		ASSERT_TRUE(warnings.empty());
		ASSERT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(built.Value().nodes[1].binding.value));
		const auto &binding = std::get<PathBinding::CopyVacancy>(built.Value().nodes[1].binding.value);
		EXPECT_EQ(binding.vacancyIndex, 1u);
		EXPECT_EQ(binding.offset, glm::vec3(1, 2, 3));
		EXPECT_FLOAT_EQ(binding.buffer, 1.25f);
	}

	TEST(PathVacancyBindingTests, SelectedVacancyEndsUseCurrentPathBuffer)
	{
		for (const bool atomStart : {false, true})
		{
			RendererWindowState window;
			window.structure.atoms = {{"C", glm::vec3(10, 0, 0)}};
			window.structure.vacancies.resize(2);
			window.structure.vacancies[1].cartesianPosition = glm::vec3(5, 0, 0);
			if (atomStart)
				window.selectedAtomIndices = {0};
			window.selectedVacancies = atomStart ? std::vector<std::size_t>{1} : std::vector<std::size_t>{0, 1};
			const auto added = AddScenePathThroughSelectedAtoms(window, true);
			ASSERT_TRUE(added);
			const auto &path = *window.paths->Store().Find(added.Value());
			for (std::size_t index = atomStart ? 1 : 0; index < 2; ++index)
			{
				ASSERT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(path.nodes[index].binding.value));
				const auto &binding = std::get<PathBinding::CopyVacancy>(path.nodes[index].binding.value);
				EXPECT_EQ(binding.vacancyIndex, index);
				EXPECT_FLOAT_EQ(binding.buffer, GetScenePathAtomBuffer());
			}
		}
	}

	TEST(PathVacancyBindingTests, StalePersistedVacancyRemainsBrokenWithStoredFallback)
	{
		ScenePath path = Line();
		path.nodes[0].binding.value = PathBinding::CopyVacancy{99, {}, 1};
		RendererStructureData structure;
		std::vector<StructuredError> warnings;
		const auto built = BuildScenePath(ExtractPersistedScenePath(path, structure), structure, warnings);
		ASSERT_TRUE(built);
		EXPECT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(built.Value().nodes[0].binding.value));
		const auto resolved = ResolveNodePositions(built.Value(), BindingContext{});
		EXPECT_EQ(resolved.positions[0], glm::vec3(0));
		ASSERT_EQ(resolved.diagnostics.size(), 1u);
		EXPECT_EQ(resolved.diagnostics[0].code, PathDiagnosticCode::BrokenBinding);
	}

	TEST(PathVacancyBindingTests, VacancyBindingEditsValidateIndexBufferAndEndpoint)
	{
		RendererWindowState window;
		window.structure.vacancies.resize(1);
		ScenePath path = Line();
		path.nodes.insert(path.nodes.begin() + 1, {PathElementId{4}, glm::vec3(5, 0, 0), {}});
		path.segments.push_back({PathElementId{5}, LineSegmentData{}});
		path.nextElementId = 6;
		const auto id = SceneSystem::AppendScenePath(window, path);
		window.pathEdit.Enter(id);
		window.pathEdit.SetSelection({path.nodes[1].id});
		EXPECT_FALSE(SetActiveScenePathNodeBinding(window, PathBinding{PathBinding::CopyVacancy{1, {}, 0}}));
		EXPECT_FALSE(SetActiveScenePathNodeBinding(window, PathBinding{PathBinding::CopyVacancy{0, {}, 1}}));
		window.pathEdit.SetSelection({path.nodes[0].id});
		EXPECT_FALSE(SetActiveScenePathNodeBinding(window,
			PathBinding{PathBinding::CopyVacancy{0, {}, std::numeric_limits<float>::quiet_NaN()}}));
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(window.paths->Store().Find(id)->nodes[1].binding.value));
		window.pathEdit.SetSelection({path.nodes[1].id});
		EXPECT_TRUE(SetActiveScenePathNodeBinding(window, PathBinding{PathBinding::CopyVacancy{0, {}, 0}}));
	}

	TEST(PathVacancyBindingTests, InsertingNodesPreservesBufferedVacancyEndpoint)
	{
		RendererWindowState window;
		window.structure.vacancies.resize(1);
		window.structure.vacancies[0].radius = 2;
		ScenePath path = Line();
		path.nodes[0].binding.value = PathBinding::CopyVacancy{0, {}, 10};
		const auto id = SceneSystem::AppendScenePath(window, path);
		const auto context = SceneSystem::MakePathBindingContext(window);
		const auto before = ResolveNodePositions(*window.paths->Store().Find(id), context);
		ASSERT_TRUE(InsertScenePathNodes(MakeSilentPathEditContext(window), id, 0, 1));
		const auto &edited = *window.paths->Store().Find(id);
		ASSERT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(edited.nodes[0].binding.value));
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(edited.nodes[0].binding.value).buffer, 10);
		const auto after = ResolveNodePositions(edited, context);
		EXPECT_NEAR(glm::distance(after.positions[0], before.positions[0]), 0, 1e-5f);
	}

	TEST(PathVacancyBindingTests, DetachVacancyKeepsResolvedPosition)
	{
		RendererWindowState window;
		window.structure.vacancies.resize(1);
		ScenePath path = Line();
		path.nodes[0].binding.value = PathBinding::CopyVacancy{0, {}, 1};
		const auto id = SceneSystem::AppendScenePath(window, path);
		window.pathEdit.Enter(id);
		window.pathEdit.SetSelection({path.nodes[0].id});
		const auto before = ResolveNodePositions(*window.paths->Store().Find(id), SceneSystem::MakePathBindingContext(window));
		ASSERT_TRUE(DetachActiveScenePathNodeKeepingPosition(window));
		const auto &detached = *window.paths->Store().Find(id);
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(detached.nodes[0].binding.value));
		window.structure.vacancies[0].cartesianPosition = glm::vec3(4, 0, 0);
		EXPECT_EQ(ResolveNodePositions(detached, SceneSystem::MakePathBindingContext(window)).positions[0], before.positions[0]);
	}
}
