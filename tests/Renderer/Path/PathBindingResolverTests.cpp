#include "Core/dspch.hpp"

#include <gtest/gtest.h>

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
						EXPECT_EQ(actualData.startHandle.position, expectedData.startHandle.position);
						EXPECT_EQ(actualData.startHandle.type, expectedData.startHandle.type);
						EXPECT_EQ(actualData.endHandle.id, expectedData.endHandle.id);
						EXPECT_EQ(actualData.endHandle.position, expectedData.endHandle.position);
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
		EXPECT_EQ(resolved.positions[0], glm::vec3(5.0f, 2.0f, 3.0f));
		EXPECT_EQ(resolved.positions[1], glm::vec3(6.0f, 0.0f, 0.0f));
		EXPECT_EQ(resolved.positions[2], glm::vec3(8.0f, 0.0f, 0.0f));
	}

	TEST(PathBindingResolverTests, EndpointBufferUsesUnbufferedNeighbourAndNeverInverts)
	{
		ScenePath path = MakePath();
		path.nodes.resize(2);
		path.segments.resize(1);
		path.nodes[0].binding.value = PathBinding::CopyPosition{0, {}, 10.0f};
		const ResolvedNodes buffered = ResolveNodePositions(path, MakeContext());
		EXPECT_GT(buffered.positions[0].x, 4.0f);
		EXPECT_LT(buffered.positions[0].x, path.nodes[1].position.x);

		ScenePath interior = MakePath();
		interior.nodes[1].binding.value = PathBinding::CopyPosition{0, {}, 1.0f};
		const ResolvedNodes interiorResolved = ResolveNodePositions(interior, MakeContext());
		EXPECT_EQ(interiorResolved.positions[1], glm::vec3(4.0f, 0.0f, 0.0f));
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
} // namespace DefectStudio::Tests
