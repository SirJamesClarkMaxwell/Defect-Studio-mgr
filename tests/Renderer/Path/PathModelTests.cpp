#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <charconv>
#include <limits>
#include <string>

#include "Renderer/Path/PathEvaluator.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakeLinePath()
		{
			ScenePath path;
			path.nodes = {
				{PathElementId{1}, glm::vec3(0.0f), {}},
				{PathElementId{2}, glm::vec3(1.0f, 0.0f, 0.0f), {}}};
			path.segments = {{PathElementId{3}, LineSegmentData{}}};
			path.nextElementId = 4;
			return path;
		}

		bool HasDiagnostic(const std::vector<PathDiagnostic> &diagnostics, PathDiagnosticCode code)
		{
			for (const PathDiagnostic &diagnostic : diagnostics)
			{
				if (diagnostic.code == code)
					return true;
			}
			return false;
		}
	} // namespace

	TEST(PathModelTests, AllocateElementIdIsMonotonicAndNeverReusesAnId)
	{
		ScenePath path;
		EXPECT_EQ(AllocateElementId(path), PathElementId{1});
		EXPECT_EQ(AllocateElementId(path), PathElementId{2});
		EXPECT_EQ(path.nextElementId, 3u);
	}

	TEST(PathModelTests, ValidatePathReportsTopologyDuplicateAndNonFiniteAuthoredData)
	{
		ScenePath topology = MakeLinePath();
		topology.segments.clear();
		EXPECT_TRUE(HasDiagnostic(ValidatePath(topology), PathDiagnosticCode::NodeCountMismatch));

		ScenePath duplicate = MakeLinePath();
		duplicate.segments[0].id = duplicate.nodes[0].id;
		EXPECT_TRUE(HasDiagnostic(ValidatePath(duplicate), PathDiagnosticCode::DuplicateElementId));

		ScenePath nonFinite = MakeLinePath();
		nonFinite.nodes[0].position.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_TRUE(HasDiagnostic(ValidatePath(nonFinite), PathDiagnosticCode::NonFinite));
		EXPECT_TRUE(ValidatePath(MakeLinePath()).empty());
	}

	TEST(PathModelTests, SignedSweepRoundTripsBitIdenticallyAtMaxDigits10)
	{
		const float sweep = 2.0f * 3.14159265358979323846f / 3.0f;
		char text[std::numeric_limits<float>::max_digits10 + 1]{};
		const auto [end, writeError] = std::to_chars(
			text, text + sizeof(text), sweep, std::chars_format::general, std::numeric_limits<float>::max_digits10);
		ASSERT_EQ(writeError, std::errc{});
		float restored = 0.0f;
		const auto [readEnd, readError] = std::from_chars(text, end, restored);
		ASSERT_EQ(readError, std::errc{});
		EXPECT_EQ(readEnd, end);
		EXPECT_EQ(std::bit_cast<std::uint32_t>(restored), std::bit_cast<std::uint32_t>(sweep));
	}
} // namespace DefectStudio::Tests
