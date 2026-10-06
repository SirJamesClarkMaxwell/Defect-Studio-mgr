#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include <variant>

#include "Renderer/Path/PathBindingResolver.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakePath()
		{
			ScenePath path;
			path.nodes = {
				{PathElementId{1}, {1.0f, 2.0f, 3.0f}, {}},
				{PathElementId{2}, {10.0f, 0.0f, 0.0f}, {}},
				{PathElementId{3}, {20.0f, 0.0f, 0.0f}, {}}};
			path.segments = {{PathElementId{4}, LineSegmentData{}}, {PathElementId{5}, LineSegmentData{}}};
			path.nextElementId = 6;
			return path;
		}

		BindingContext MakeContext()
		{
			return {
				[](std::size_t index) -> std::optional<glm::vec3> { return index == 0 ? std::optional(glm::vec3(4.0f, 0.0f, 0.0f)) : std::nullopt; },
				[](std::size_t index) -> std::optional<float> { return index == 0 ? std::optional(2.0f) : std::nullopt; },
				[](SceneObjectId id) -> std::optional<glm::vec3> { return id.value == 9 ? std::optional(glm::vec3(7.0f, 0.0f, 0.0f)) : std::nullopt; },
				[](SceneObjectId id) { return id.value == 10; }};
		}

		bool HasDiagnostic(const ResolvedNodes &resolved, PathDiagnosticCode code)
		{
			for (const PathDiagnostic &diagnostic : resolved.diagnostics)
				if (diagnostic.code == code)
					return true;
			return false;
		}

		void ExpectBindingEqual(const PathBinding &actual, const PathBinding &expected)
		{
			ASSERT_EQ(actual.value.index(), expected.value.index());
			std::visit([&](const auto &actualBinding, const auto &expectedBinding) {
				using Actual = std::decay_t<decltype(actualBinding)>;
				using Expected = std::decay_t<decltype(expectedBinding)>;
				if constexpr (std::is_same_v<Actual, Expected> && std::is_same_v<Actual, PathBinding::CopyPosition>)
				{
					EXPECT_EQ(actualBinding.atomIndex, expectedBinding.atomIndex);
					EXPECT_EQ(actualBinding.offset, expectedBinding.offset);
					EXPECT_EQ(actualBinding.buffer, expectedBinding.buffer);
				}
				else if constexpr (std::is_same_v<Actual, Expected> && std::is_same_v<Actual, PathBinding::BondMidpoint>)
				{
					EXPECT_EQ(actualBinding.atomA, expectedBinding.atomA);
					EXPECT_EQ(actualBinding.atomB, expectedBinding.atomB);
					EXPECT_EQ(actualBinding.offset, expectedBinding.offset);
				}
				else if constexpr (std::is_same_v<Actual, Expected> && std::is_same_v<Actual, PathBinding::ObjectOrigin>)
				{
					EXPECT_EQ(actualBinding.object, expectedBinding.object);
					EXPECT_EQ(actualBinding.offset, expectedBinding.offset);
				}
			}, actual.value, expected.value);
		}

		void ExpectPathUnchanged(const ScenePath &actual, const ScenePath &expected)
		{
			EXPECT_EQ(actual.id, expected.id);
			EXPECT_EQ(actual.persistKey, expected.persistKey);
			EXPECT_EQ(actual.name, expected.name);
			EXPECT_EQ(actual.nextElementId, expected.nextElementId);
			EXPECT_EQ(actual.visible, expected.visible);
			EXPECT_EQ(actual.renderable, expected.renderable);
			ASSERT_EQ(actual.nodes.size(), expected.nodes.size());
			ASSERT_EQ(actual.segments.size(), expected.segments.size());
			for (std::size_t index = 0; index < actual.nodes.size(); ++index)
			{
				EXPECT_EQ(actual.nodes[index].id, expected.nodes[index].id);
				EXPECT_EQ(actual.nodes[index].position, expected.nodes[index].position);
				ExpectBindingEqual(actual.nodes[index].binding, expected.nodes[index].binding);
			}
			for (std::size_t index = 0; index < actual.segments.size(); ++index)
			{
				EXPECT_EQ(actual.segments[index].id, expected.segments[index].id);
				ASSERT_EQ(actual.segments[index].data.index(), expected.segments[index].data.index());
				std::visit([&](const auto &actualData, const auto &expectedData) {
					using Actual = std::decay_t<decltype(actualData)>;
					using Expected = std::decay_t<decltype(expectedData)>;
					if constexpr (std::is_same_v<Actual, Expected> && std::is_same_v<Actual, CubicBezierSegmentData>)
					{
						EXPECT_EQ(actualData.startHandle.id, expectedData.startHandle.id);
						EXPECT_EQ(actualData.startHandle.offset, expectedData.startHandle.offset);
						EXPECT_EQ(actualData.startHandle.type, expectedData.startHandle.type);
						EXPECT_EQ(actualData.endHandle.id, expectedData.endHandle.id);
						EXPECT_EQ(actualData.endHandle.offset, expectedData.endHandle.offset);
						EXPECT_EQ(actualData.endHandle.type, expectedData.endHandle.type);
					}
					else if constexpr (std::is_same_v<Actual, Expected> && std::is_same_v<Actual, CircularArcSegmentData>)
					{
						EXPECT_EQ(actualData.planeNormal, expectedData.planeNormal);
						EXPECT_EQ(actualData.signedSweepRadians, expectedData.signedSweepRadians);
					}
				}, actual.segments[index].data, expected.segments[index].data);
			}
		}

		[[nodiscard]] glm::vec3 TransformLocal(const ScenePath &path, const glm::vec3 &position)
		{
			return path.transform.position + path.transform.rotation * (path.transform.scale * position);
		}

		[[nodiscard]] glm::vec3 TransformOffset(const ScenePath &path, const glm::vec3 &offset)
		{
			return path.transform.rotation * (path.transform.scale * offset);
		}

		void ExpectVec3Near(const glm::vec3 &actual, const glm::vec3 &expected)
		{
			EXPECT_NEAR(actual.x, expected.x, 1.0e-5f);
			EXPECT_NEAR(actual.y, expected.y, 1.0e-5f);
			EXPECT_NEAR(actual.z, expected.z, 1.0e-5f);
		}
	} // namespace

	TEST(PathBindingResolverTests, ResolvesFreeCopyMidpointAndObjectOriginBindings)
	{
		const ScenePath free = MakePath();
		EXPECT_EQ(ResolveNodePositions(free, MakeContext()).positions[0], free.nodes[0].position);

		ScenePath path = MakePath();
		path.nodes[0].binding.value = PathBinding::CopyPosition{0, {1.0f, 2.0f, 3.0f}, 0.0f};
		path.nodes[1].binding.value = PathBinding::BondMidpoint{0, 0, {2.0f, 0.0f, 0.0f}};
		path.nodes[2].binding.value = PathBinding::ObjectOrigin{SceneObjectId{9}, {1.0f, 0.0f, 0.0f}};
		const ResolvedNodes resolved = ResolveNodePositions(path, MakeContext());
		ASSERT_EQ(resolved.positions.size(), path.nodes.size());
		const auto &copy = std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value);
		const auto &midpoint = std::get<PathBinding::BondMidpoint>(path.nodes[1].binding.value);
		const auto &origin = std::get<PathBinding::ObjectOrigin>(path.nodes[2].binding.value);
		const glm::vec3 atom = MakeContext().atomPosition(0).value();
		const glm::vec3 originPosition = MakeContext().objectOrigin(SceneObjectId{9}).value();
		EXPECT_EQ(resolved.positions[0], atom + copy.offset);
		EXPECT_EQ(resolved.positions[1], atom + midpoint.offset);
		EXPECT_EQ(resolved.positions[2], originPosition + origin.offset);
	}

	TEST(PathBindingResolverTests, EndpointBufferUsesUnbufferedNeighbourAndNeverInverts)
	{
		ScenePath path = MakePath();
		path.nodes.resize(2);
		ASSERT_EQ(path.nodes.size(), 2u);
		path.segments.resize(1);
		path.nodes[0].binding.value = PathBinding::CopyPosition{0, {}, 10.0f};
		const ResolvedNodes buffered = ResolveNodePositions(path, MakeContext());
		EXPECT_GT(buffered.positions[0].x, 4.0f);
		EXPECT_LT(buffered.positions[0].x, path.nodes[1].position.x);

		ScenePath interior = MakePath();
		interior.nodes[1].binding.value = PathBinding::CopyPosition{0, {}, 1.0f};
		const ResolvedNodes interiorResolved = ResolveNodePositions(interior, MakeContext());
		ASSERT_EQ(interiorResolved.positions.size(), interior.nodes.size());
		EXPECT_EQ(interiorResolved.positions[1], MakeContext().atomPosition(0).value());
		EXPECT_TRUE(HasDiagnostic(interiorResolved, PathDiagnosticCode::InteriorNodeBuffer));
	}

	TEST(PathBindingResolverTests, BrokenAndPathObjectBindingsFallBackWithoutMutatingAuthoredPath)
	{
		ScenePath path = MakePath();
		path.nodes[0].binding.value = PathBinding::CopyPosition{1, {}, 0.0f};
		path.nodes[1].binding.value = PathBinding::ObjectOrigin{SceneObjectId{10}, {}};
		const ScenePath authored = path;
		const ResolvedNodes resolved = ResolveNodePositions(path, MakeContext());
		EXPECT_EQ(resolved.positions[0], authored.nodes[0].position);
		EXPECT_EQ(resolved.positions[1], authored.nodes[1].position);
		EXPECT_TRUE(HasDiagnostic(resolved, PathDiagnosticCode::BrokenBinding));
		EXPECT_TRUE(HasDiagnostic(resolved, PathDiagnosticCode::ObjectOriginTargetsPath));
		ExpectPathUnchanged(path, authored);
	}

	// Criterion 1: identity preserves the old authored free positions and binding world positions.
	TEST(PathBindingResolverTests, IdentityTransformPreservesFreeAndBoundPositions)
	{
		ScenePath path = MakePath();
		path.nodes.resize(2);
		ASSERT_EQ(path.nodes.size(), 2u);
		path.nodes[1].binding.value = PathBinding::CopyPosition{0, glm::vec3(-0.5f, 1.0f, 0.25f), 0.0f};
		const glm::vec3 atomPosition{4.0f, 0.0f, 0.0f};
		BindingContext context = MakeContext();
		context.atomPosition = [atomPosition](std::size_t) -> std::optional<glm::vec3> { return atomPosition; };

		const ResolvedNodes resolved = ResolveNodePositions(path, context);
		ASSERT_EQ(resolved.positions.size(), path.nodes.size());
		EXPECT_EQ(resolved.positions[0], path.nodes[0].position);
		const auto &binding = std::get<PathBinding::CopyPosition>(path.nodes[1].binding.value);
		EXPECT_EQ(resolved.positions[1], atomPosition + binding.offset);
	}

	// Criterion 2: free authored positions are transformed from path-local space.
	TEST(PathBindingResolverTests, FreeNodeUsesPathTransform)
	{
		ScenePath path = MakePath();
		path.nodes.resize(1);
		ASSERT_EQ(path.nodes.size(), 1u);
		path.transform.position = glm::vec3(3.0f, -4.0f, 2.0f);
		path.transform.rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
		path.transform.scale = glm::vec3(2.0f, 0.5f, 3.0f);

		const ResolvedNodes resolved = ResolveNodePositions(path, BindingContext{});
		ASSERT_EQ(resolved.positions.size(), 1u);
		ExpectVec3Near(resolved.positions[0], TransformLocal(path, path.nodes[0].position));
	}

	// Criterion 3: binding world positions are not path-local and therefore ignore G/R/S.
	TEST(PathBindingResolverTests, BoundNodeIgnoresPathTransform)
	{
		ScenePath path = MakePath();
		path.nodes.resize(1);
		ASSERT_EQ(path.nodes.size(), 1u);
		path.nodes[0].binding.value = PathBinding::CopyPosition{0, glm::vec3(0.25f, -0.5f, 1.0f), 0.0f};
		const glm::vec3 atomPosition{7.0f, -2.0f, 5.0f};
		BindingContext context;
		context.atomPosition = [atomPosition](std::size_t) -> std::optional<glm::vec3> { return atomPosition; };
		path.transform.position = glm::vec3(-10.0f, 8.0f, 3.0f);
		path.transform.rotation = glm::angleAxis(glm::radians(83.0f), glm::normalize(glm::vec3(2.0f, -1.0f, 4.0f)));
		path.transform.scale = glm::vec3(0.25f, 4.0f, 1.75f);

		const auto &binding = std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value);
		const ResolvedNodes resolved = ResolveNodePositions(path, context);
		ASSERT_EQ(resolved.positions.size(), 1u);
		ExpectVec3Near(resolved.positions[0], atomPosition + binding.offset);
	}

	// Criterion 4: this looks like a bug on first inspection, but all-bound paths deform instead of travelling.
	TEST(PathBindingResolverTests, AllBoundPathDeformsInsteadOfFollowingPathTransform)
	{
		ScenePath path = MakePath();
		path.nodes.resize(2);
		ASSERT_EQ(path.nodes.size(), 2u);
		const glm::vec3 firstAtom{2.0f, 1.0f, -3.0f};
		const glm::vec3 secondAtom{-4.0f, 5.0f, 6.0f};
		path.nodes[0].binding.value = PathBinding::CopyPosition{0, glm::vec3(0.5f, 0.0f, -0.25f), 0.0f};
		path.nodes[1].binding.value = PathBinding::CopyPosition{1, glm::vec3(-0.25f, 0.75f, 0.5f), 0.0f};
		BindingContext context;
		context.atomPosition = [firstAtom, secondAtom](std::size_t index) -> std::optional<glm::vec3> {
			return index == 0 ? std::optional(firstAtom) : index == 1 ? std::optional(secondAtom) : std::nullopt;
		};
		path.transform.position = glm::vec3(20.0f, -12.0f, 8.0f);
		path.transform.rotation = glm::angleAxis(glm::radians(61.0f), glm::normalize(glm::vec3(-1.0f, 3.0f, 2.0f)));
		path.transform.scale = glm::vec3(3.0f, 0.4f, 2.0f);

		const ResolvedNodes resolved = ResolveNodePositions(path, context);
		ASSERT_EQ(resolved.positions.size(), path.nodes.size());
		const auto &firstBinding = std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value);
		const auto &secondBinding = std::get<PathBinding::CopyPosition>(path.nodes[1].binding.value);
		ExpectVec3Near(resolved.positions[0], firstAtom + firstBinding.offset);
		ExpectVec3Near(resolved.positions[1], secondAtom + secondBinding.offset);
	}

	// Criterion 5: offsets are transformed and anchored at the resolved node, including a bound node.
	TEST(PathBindingResolverTests, HandleUsesResolvedNodeAndTransformedOffsetForFreeAndBoundNodes)
	{
		ScenePath path = MakePath();
		path.nodes.resize(2);
		ASSERT_EQ(path.nodes.size(), 2u);
		path.nodes[0].position = glm::vec3(1.0f, -2.0f, 0.5f);
		path.nodes[1].position = glm::vec3(99.0f, 98.0f, 97.0f);
		path.nodes[1].binding.value = PathBinding::CopyPosition{0, glm::vec3(-0.5f, 0.25f, 1.0f), 0.0f};
		path.segments = {{PathElementId{4}, CubicBezierSegmentData{
			{PathElementId{5}, glm::vec3(0.75f, 1.0f, -0.5f), BezierHandleType::Free},
			{PathElementId{6}, glm::vec3(-1.0f, 0.5f, 0.25f), BezierHandleType::Free}}}};
		const glm::vec3 atomPosition{4.0f, 3.0f, -2.0f};
		BindingContext context;
		context.atomPosition = [atomPosition](std::size_t) -> std::optional<glm::vec3> { return atomPosition; };
		path.transform.position = glm::vec3(-2.0f, 5.0f, 1.0f);
		path.transform.rotation = glm::angleAxis(glm::radians(29.0f), glm::normalize(glm::vec3(2.0f, 1.0f, -3.0f)));
		path.transform.scale = glm::vec3(1.5f, 0.75f, 2.25f);

		const ResolvedNodes resolved = ResolveNodePositions(path, context);
		ASSERT_EQ(resolved.positions.size(), 2u);
		ASSERT_EQ(resolved.handlePositions.size(), 2u);
		const auto &cubic = std::get<CubicBezierSegmentData>(path.segments[0].data);
		ExpectVec3Near(resolved.handlePositions[0], resolved.positions[0] + TransformOffset(path, cubic.startHandle.offset));
		ExpectVec3Near(resolved.handlePositions[1], resolved.positions[1] + TransformOffset(path, cubic.endHandle.offset));
	}

	// Criterion 6: positions and handles use the same non-uniform local scale before rotation.
	TEST(PathBindingResolverTests, NonUniformScaleUsesTheSameTransformForNodesAndHandleOffsets)
	{
		ScenePath path = MakePath();
		path.nodes.resize(2);
		ASSERT_EQ(path.nodes.size(), 2u);
		path.nodes[0].position = glm::vec3(-1.0f, 2.0f, 3.0f);
		path.nodes[1].position = glm::vec3(4.0f, -2.0f, 1.0f);
		path.segments = {{PathElementId{4}, CubicBezierSegmentData{
			{PathElementId{5}, glm::vec3(0.5f, -1.0f, 2.0f), BezierHandleType::Free},
			{PathElementId{6}, glm::vec3(-2.0f, 0.25f, 0.75f), BezierHandleType::Free}}}};
		path.transform.rotation = glm::angleAxis(glm::radians(47.0f), glm::normalize(glm::vec3(1.0f, -2.0f, 1.5f)));
		path.transform.scale = glm::vec3(2.0f, 0.5f, 3.0f);

		const ResolvedNodes resolved = ResolveNodePositions(path, BindingContext{});
		ASSERT_EQ(resolved.positions.size(), 2u);
		ASSERT_EQ(resolved.handlePositions.size(), 2u);
		const auto &cubic = std::get<CubicBezierSegmentData>(path.segments[0].data);
		ExpectVec3Near(resolved.positions[0], TransformLocal(path, path.nodes[0].position));
		ExpectVec3Near(resolved.positions[1], TransformLocal(path, path.nodes[1].position));
		ExpectVec3Near(resolved.handlePositions[0] - resolved.positions[0], TransformOffset(path, cubic.startHandle.offset));
		ExpectVec3Near(resolved.handlePositions[1] - resolved.positions[1], TransformOffset(path, cubic.endHandle.offset));
	}
} // namespace DefectStudio::Tests
