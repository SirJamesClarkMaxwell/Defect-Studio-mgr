#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "Renderer/Path/PathBindingResolver.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] ScenePath TwoNodePath()
		{
			ScenePath path;
			path.id = SceneObjectId{1};
			path.nodes.push_back(PathNode{AllocateElementId(path), glm::vec3(-1.0f, 0.0f, 0.0f), {}});
			path.nodes.push_back(PathNode{AllocateElementId(path), glm::vec3(1.0f, 0.0f, 0.0f), {}});
			path.segments.push_back(PathSegment{AllocateElementId(path), LineSegmentData{}});
			return path;
		}

		[[nodiscard]] BindingContext AtomsAt(std::vector<glm::vec3> atoms)
		{
			BindingContext context;
			context.atomPosition = [atoms](const std::size_t index) -> std::optional<glm::vec3> {
				if (index >= atoms.size())
					return std::nullopt;
				return atoms[index];
			};
			context.atomRadius = [](std::size_t) -> std::optional<float> { return 0.5f; };
			return context;
		}

		[[nodiscard]] std::uint64_t RevisionFor(const ScenePath &path, const BindingContext &context)
		{
			return BindingSourceRevision(path, ResolveNodePositions(path, context));
		}
	} // namespace

	TEST(PathBindingSourceRevisionTests, FreePathIsZeroWhateverTheContext)
	{
		const ScenePath path = TwoNodePath();
		EXPECT_EQ(RevisionFor(path, BindingContext{}), 0u);
		EXPECT_EQ(RevisionFor(path, AtomsAt({glm::vec3(4.0f)})), 0u);
	}

	TEST(PathBindingSourceRevisionTests, BoundPathIsStableForTheSamePositions)
	{
		ScenePath path = TwoNodePath();
		path.nodes.back().binding.value = PathBinding::CopyPosition{0, glm::vec3(0.0f), 0.0f};

		const BindingContext context = AtomsAt({glm::vec3(2.0f, 3.0f, 4.0f)});
		const std::uint64_t first = RevisionFor(path, context);
		EXPECT_NE(first, 0u);
		EXPECT_EQ(RevisionFor(path, context), first);
		EXPECT_EQ(RevisionFor(path, AtomsAt({glm::vec3(2.0f, 3.0f, 4.0f)})), first);
	}

	TEST(PathBindingSourceRevisionTests, ChangesWhenTheBoundAtomMoves)
	{
		ScenePath path = TwoNodePath();
		path.nodes.back().binding.value = PathBinding::CopyPosition{0, glm::vec3(0.0f), 0.0f};

		EXPECT_NE(
			RevisionFor(path, AtomsAt({glm::vec3(2.0f, 3.0f, 4.0f)})),
			RevisionFor(path, AtomsAt({glm::vec3(2.0f, 3.0f, 4.5f)})));
	}

	TEST(PathBindingSourceRevisionTests, BrokenBindingDiffersFromAResolvedOneAndIsStable)
	{
		ScenePath path = TwoNodePath();
		path.nodes.back().binding.value = PathBinding::CopyPosition{7, glm::vec3(0.0f), 0.0f};

		const std::uint64_t broken = RevisionFor(path, AtomsAt({}));
		EXPECT_EQ(RevisionFor(path, AtomsAt({})), broken);
		EXPECT_NE(RevisionFor(path, AtomsAt(std::vector<glm::vec3>(8, glm::vec3(9.0f)))), broken);
	}
} // namespace DefectStudio::Tests
