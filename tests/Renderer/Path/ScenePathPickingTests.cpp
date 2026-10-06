#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/Path/ScenePathPicking.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr glm::vec2 kViewport{800.0f, 600.0f};

		// Camera on +Z looking at the origin with +Y up, so world +X is exactly the camera's right
		// and the origin lands on the middle pixel. Same convention as PathPickingTests.
		[[nodiscard]] glm::mat4 ViewProjection()
		{
			return glm::perspective(glm::radians(45.0f), kViewport.x / kViewport.y, 0.1f, 100.0f) *
				glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		[[nodiscard]] PathPickSettings Settings(const glm::vec2 cursor)
		{
			PathPickSettings settings;
			settings.viewProjection = ViewProjection();
			settings.viewportSize = kViewport;
			settings.cursor = cursor;
			settings.cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
			settings.editMode = false;
			return settings;
		}

		// A straight horizontal line from -1 to +1 at the given z, thick enough to be an easy target.
		[[nodiscard]] ScenePath StraightPath(const std::uint64_t id, const float z)
		{
			ScenePath path;
			path.id = SceneObjectId{id};
			path.style.width = 0.2f;

			PathNode start;
			start.id = AllocateElementId(path);
			start.position = glm::vec3(-1.0f, 0.0f, z);
			PathNode end;
			end.id = AllocateElementId(path);
			end.position = glm::vec3(1.0f, 0.0f, z);
			path.nodes = {start, end};

			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}

		// Inserts `path` and primes the cache the way a completed render pass would have. The key is
		// arbitrary on purpose: FindLastBuilt must not care which one produced the entry.
		void InsertAndPrime(PathSystem &system, ScenePath path)
		{
			const SceneObjectId id = path.id;
			ASSERT_TRUE(system.Store().Insert(std::move(path)));
			const ScenePath &stored = *system.Store().Find(id);
			const ResolvedNodes resolved = ResolveNodePositions(stored, BindingContext{});
			const EvaluatedPath evaluated = Tessellate(stored, resolved, TessellationSettings{});
			system.Caches().Store(id, PathEvaluationKey{system.Store().RevisionsFor(id), 0, 3},
				CachedPathGeometry{evaluated, {}});
		}

		void InsertWithoutPriming(PathSystem &system, ScenePath path)
		{
			ASSERT_TRUE(system.Store().Insert(std::move(path)));
		}
	} // namespace

	// Criterion 1.
	TEST(PathCachesTests, FindLastBuiltIgnoresTheKeyAndFindStillDoesNot)
	{
		PathCaches caches;
		const SceneObjectId id{7};
		const PathEvaluationKey stored{PathRevisions{4, 2}, 0, 5};
		EvaluatedPath evaluated;
		evaluated.totalLength = 12.5;
		caches.Store(id, stored, CachedPathGeometry{evaluated, {}});

		const PathEvaluationKey other{PathRevisions{4, 2}, 0, 6};
		EXPECT_EQ(caches.Find(id, other), nullptr);
		const CachedPathGeometry *last = caches.FindLastBuilt(id);
		ASSERT_NE(last, nullptr);
		EXPECT_DOUBLE_EQ(last->evaluated.totalLength, 12.5);
		EXPECT_EQ(caches.FindLastBuilt(SceneObjectId{99}), nullptr);
	}

	// Criterion 2.
	TEST(ScenePathPickingTests, EmptySystemPicksNothing)
	{
		PathSystem system;
		EXPECT_FALSE(PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{}).has_value());
	}

	// Criterion 3.
	TEST(ScenePathPickingTests, CursorOffEveryPathPicksNothing)
	{
		PathSystem system;
		InsertAndPrime(system, StraightPath(1, 0.0f));
		EXPECT_FALSE(PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 50.0f)), BindingContext{}).has_value());
	}

	// Criterion 4.
	TEST(ScenePathPickingTests, CursorOnTheShaftPicksThatPath)
	{
		PathSystem system;
		InsertAndPrime(system, StraightPath(1, 0.0f));

		const std::optional<ScenePathPick> pick = PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{});
		ASSERT_TRUE(pick.has_value());
		EXPECT_EQ(pick->path, SceneObjectId{1});
		EXPECT_EQ(pick->result.kind, PathPickKind::WholePath);
		EXPECT_EQ(pick->result.path, SceneObjectId{1});
	}

	// Criterion 5: the nearer path wins whichever order the store holds them in.
	TEST(ScenePathPickingTests, FrontmostPathWinsRegardlessOfStoreOrder)
	{
		{
			PathSystem system;
			InsertAndPrime(system, StraightPath(1, 0.0f));  // further
			InsertAndPrime(system, StraightPath(2, 2.0f));  // nearer the camera at z == 5
			const std::optional<ScenePathPick> pick =
				PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{});
			ASSERT_TRUE(pick.has_value());
			EXPECT_EQ(pick->path, SceneObjectId{2});
		}
		{
			PathSystem system;
			InsertAndPrime(system, StraightPath(2, 2.0f));
			InsertAndPrime(system, StraightPath(1, 0.0f));
			const std::optional<ScenePathPick> pick =
				PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{});
			ASSERT_TRUE(pick.has_value());
			EXPECT_EQ(pick->path, SceneObjectId{2});
		}
	}

	// Criterion 6.
	TEST(ScenePathPickingTests, DepthIsNdcAndTheNearerHitReportsTheSmallerValue)
	{
		PathSystem system;
		InsertAndPrime(system, StraightPath(1, 0.0f));
		const std::optional<ScenePathPick> distant =
			PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{});

		PathSystem nearSystem;
		InsertAndPrime(nearSystem, StraightPath(1, 2.0f));
		const std::optional<ScenePathPick> close =
			PickFrontmostScenePath(nearSystem, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{});

		ASSERT_TRUE(distant.has_value());
		ASSERT_TRUE(close.has_value());
		EXPECT_LT(close->depth, distant->depth);
		EXPECT_GT(distant->depth, -1.0f);
		EXPECT_LT(distant->depth, 1.0f);
	}

	// Criterion 7.
	TEST(ScenePathPickingTests, HiddenAndNonRenderablePathsAreNotPickable)
	{
		{
			PathSystem system;
			ScenePath path = StraightPath(1, 0.0f);
			path.visible = false;
			InsertAndPrime(system, std::move(path));
			EXPECT_FALSE(PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{}).has_value());
		}
		{
			PathSystem system;
			ScenePath path = StraightPath(1, 0.0f);
			path.renderable = false;
			InsertAndPrime(system, std::move(path));
			EXPECT_FALSE(PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{}).has_value());
		}
	}

	// Criterion 8: the documented ceiling - a path the render pass never reached has no shaft.
	TEST(ScenePathPickingTests, PathWithNoCachedGeometryOffersNoShaft)
	{
		PathSystem system;
		InsertWithoutPriming(system, StraightPath(1, 0.0f));
		EXPECT_FALSE(PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{}).has_value());
	}

	// Criterion 9: a hidden path in front must not shadow a visible one behind it.
	TEST(ScenePathPickingTests, HiddenPathInFrontDoesNotShadowTheVisibleOneBehind)
	{
		PathSystem system;
		InsertAndPrime(system, StraightPath(1, 0.0f));
		ScenePath front = StraightPath(2, 2.0f);
		front.visible = false;
		InsertAndPrime(system, std::move(front));

		const std::optional<ScenePathPick> pick = PickFrontmostScenePath(system, Settings(glm::vec2(400.0f, 300.0f)), BindingContext{});
		ASSERT_TRUE(pick.has_value());
		EXPECT_EQ(pick->path, SceneObjectId{1});
	}

	// Criterion 10.
	TEST(ScenePathPickingTests, MalformedSettingsPickNothing)
	{
		PathSystem system;
		InsertAndPrime(system, StraightPath(1, 0.0f));

		PathPickSettings degenerate = Settings(glm::vec2(400.0f, 300.0f));
		degenerate.viewportSize = glm::vec2(0.0f);
		EXPECT_FALSE(PickFrontmostScenePath(system, degenerate, BindingContext{}).has_value());
	}

	// Criterion 11: edit mode reports the element kind rather than collapsing to WholePath.
	TEST(ScenePathPickingTests, EditModeReportsTheElementUnderTheCursor)
	{
		PathSystem system;
		InsertAndPrime(system, StraightPath(1, 0.0f));

		PathPickSettings settings = Settings(glm::vec2(400.0f, 300.0f));
		settings.editMode = true;
		const std::optional<ScenePathPick> pick = PickFrontmostScenePath(system, settings, BindingContext{});
		ASSERT_TRUE(pick.has_value());
		EXPECT_NE(pick->result.kind, PathPickKind::WholePath);
		EXPECT_NE(pick->result.kind, PathPickKind::None);
	}

	// S14: picking resolves bound nodes against the same live context the render pass uses. The
	// path is authored far off screen; its end node follows an atom sitting on the origin, which
	// projects to the middle pixel. Unprimed, so only Edit Mode's node markers are pickable.
	TEST(ScenePathPickingTests, BoundNodeIsPickedWhereItsAtomIsNotWhereItWasAuthored)
	{
		PathSystem system;
		ScenePath path = StraightPath(1, 0.0f);
		for (PathNode &node : path.nodes)
			node.position += glm::vec3(0.0f, 40.0f, 0.0f);
		path.nodes.back().binding.value = PathBinding::CopyPosition{0, glm::vec3(0.0f), 0.0f};
		const PathElementId boundNode = path.nodes.back().id;
		InsertWithoutPriming(system, std::move(path));

		BindingContext atoms;
		atoms.atomPosition = [](const std::size_t index) -> std::optional<glm::vec3> {
			return index == 0 ? std::optional<glm::vec3>(glm::vec3(0.0f)) : std::nullopt;
		};
		atoms.atomRadius = [](std::size_t) -> std::optional<float> { return 0.5f; };

		PathPickSettings settings = Settings(glm::vec2(400.0f, 300.0f));
		settings.editMode = true;

		const std::optional<ScenePathPick> bound = PickFrontmostScenePath(system, settings, atoms);
		ASSERT_TRUE(bound.has_value());
		EXPECT_EQ(bound->result.kind, PathPickKind::Node);
		EXPECT_EQ(bound->result.element, boundNode);

		EXPECT_FALSE(PickFrontmostScenePath(system, settings, BindingContext{}).has_value());
	}
} // namespace DefectStudio::Tests

namespace DefectStudio::Tests
{
	TEST(ScenePathPickingTests, ThinCurvedBoundAndBondFrameStrokesHitTwoPixelsFromTheirRenderedPolyline)
	{
		for (bool bondFrame : {false, true})
			for (bool perspective : {false, true})
			{
				PathSystem system;
				ScenePath path = StraightPath(77, 0.0f);
				path.style.width = 0.03f;
				path.segments[0].data = CircularArcSegmentData{glm::vec3(0, 0, 1), glm::radians(100.0f)};
				BindingContext bindings;
				bindings.atomPosition = [](std::size_t i) -> std::optional<glm::vec3> {
					return i == 0 ? glm::vec3(-1, 0, -0.5f) : glm::vec3(1, 0.8f, 0.5f);
				};
				bindings.atomRadius = [](std::size_t) -> std::optional<float> { return 0.4f; };
				if (bondFrame)
				{
					path.transform.position = {25, 30, 40}; // fallback is deliberately far from the bond
					path.transformBinding.value = PathTransformBinding::BondFrame{0, 1, glm::radians(37.0f)};
				}
				else
				{
					path.nodes[0].binding.value = PathBinding::CopyPosition{0, {}, 1.15f};
					path.nodes[1].binding.value = PathBinding::CopyPosition{1, {}, 1.15f};
					for (auto &node : path.nodes) node.position += glm::vec3(0, 40, 0);
				}
				ASSERT_TRUE(system.Store().Insert(std::move(path)));
				const auto &stored = *system.Store().Find(SceneObjectId{77});
				const auto resolved = ResolveNodePositions(stored, bindings);
				const auto evaluated = Tessellate(stored, resolved, TessellationSettings{});
				ASSERT_GE(evaluated.samples.size(), 3u);
				system.Caches().Store(stored.id, {system.Store().RevisionsFor(stored.id), BindingSourceRevision(stored, resolved), 3},
					CachedPathGeometry{evaluated, {}});
				PathPickSettings settings = Settings(glm::vec2(0));
				settings.viewProjection = (perspective ?
					glm::perspective(glm::radians(45.0f), kViewport.x / kViewport.y, 0.1f, 100.0f) :
					glm::ortho(-5.0f, 5.0f, -3.75f, 3.75f, 0.1f, 100.0f)) *
					glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0), glm::vec3(0, 1, 0));
				const std::size_t i = evaluated.samples.size() / 2;
				const auto project = [&](const glm::dvec3 &p) {
					const glm::vec4 clip = settings.viewProjection * glm::vec4(glm::vec3(p), 1);
					const glm::vec2 ndc = glm::vec2(clip) / clip.w;
					return glm::vec2((ndc.x + 1) * kViewport.x * 0.5f, (1 - ndc.y) * kViewport.y * 0.5f);
				};
				const glm::vec2 a = project(evaluated.samples[i-1].position), b = project(evaluated.samples[i].position);
				const glm::vec2 tangent = glm::normalize(b - a);
				settings.cursor = (a + b) * 0.5f + glm::vec2(-tangent.y, tangent.x) * 2.0f;
				const auto hit = PickFrontmostScenePath(system, settings, bindings);
				ASSERT_TRUE(hit);
				EXPECT_EQ(hit->path, stored.id);
				EXPECT_EQ(hit->result.kind, PathPickKind::WholePath);
				EXPECT_LE(hit->result.screenDistance, 2.01f);
				// The thin stroke alone cannot cover a cursor two pixels away at this zoom.
				settings.strokePickTolerance = 0;
				EXPECT_FALSE(PickFrontmostScenePath(system, settings, bindings));
			}
	}
}
