#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "ScientificRuntime/Python/GroupTheoryBridge.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// The NV- dangling-bond basis of groupy/tutorial/7_NV_center.ipynb: the nitrogen sits on the
		// C3 axis (so every C3v operation leaves it fixed) and the three carbons sit 120 degrees
		// apart in the plane below it. Labels follow the tutorial's {d, a, b, c}.
		[[nodiscard]] PermutationRepresentationRequest MakeNvFourBondBasis()
		{
			PermutationRepresentationRequest request;
			request.pointGroupLabel = "C3v";
			request.sites.push_back(BasisSite{"d", glm::dvec3(0.0, 0.0, 1.0)});
			for (int index = 0; index < 3; ++index)
			{
				const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) / 3.0;
				const char label = static_cast<char>('a' + index);
				request.sites.push_back(BasisSite{std::string(1, label), glm::dvec3(std::cos(angle), std::sin(angle), 0.0)});
			}
			return request;
		}

		[[nodiscard]] const IrrepMultiplicity *FindIrrep(const PointGroupReduction &reduction, const std::string &label)
		{
			for (const IrrepMultiplicity &entry : reduction.decomposition)
			{
				if (entry.irrepLabel == label)
					return &entry;
			}
			return nullptr;
		}

		[[nodiscard]] double Dot(const SymmetryAdaptedVector &left, const SymmetryAdaptedVector &right)
		{
			double sum = 0.0;
			for (std::size_t index = 0; index < left.coefficients.size(); ++index)
				sum += left.coefficients[index].numeric * right.coefficients[index].numeric;
			return sum;
		}
	} // namespace

	// Acceptance criterion 1: the reducible representation of the four dangling bonds in C3v
	// decomposes as A1 (+) A1 (+) E - two one-dimensional A1 copies and one two-dimensional E.
	TEST(GroupTheoryBridgeTests, ReducesNvFourBondBasisToTwoA1AndOneE)
	{
		const GroupTheoryBridge bridge;
		const Result<PointGroupReduction> result = bridge.ReduceRepresentation(MakeNvFourBondBasis());
		if (!result)
			GTEST_SKIP() << "groupy unavailable in current environment: " << result.Error().technicalDetails;

		EXPECT_EQ(result->pointGroupLabel, "C3v");
		EXPECT_EQ(result->groupOrder, 6);
		ASSERT_EQ(result->siteLabels.size(), 4u);

		const IrrepMultiplicity *a1 = FindIrrep(*result, "A1");
		ASSERT_NE(a1, nullptr) << "A1 missing from the decomposition";
		EXPECT_EQ(a1->multiplicity, 2);
		EXPECT_EQ(a1->dimension, 1);

		const IrrepMultiplicity *e = FindIrrep(*result, "E");
		ASSERT_NE(e, nullptr) << "E missing from the decomposition";
		EXPECT_EQ(e->multiplicity, 1);
		EXPECT_EQ(e->dimension, 2);

		// A2 must not appear at all; if it does, the permutation matrices were built wrong.
		EXPECT_EQ(FindIrrep(*result, "A2"), nullptr);

		// Dimensions have to add back up to the size of the basis.
		int totalDimension = 0;
		for (const IrrepMultiplicity &entry : result->decomposition)
			totalDimension += entry.multiplicity * entry.dimension;
		EXPECT_EQ(totalDimension, 4);
	}

	// Acceptance criterion 2: four symmetry-adapted vectors come back - two from A1 (occurrences 0
	// and 1) and the two rows of E - each carrying an exact form alongside a numeric form that is
	// coherent with it. C++ cannot evaluate a SymPy string, so coherence is checked structurally:
	// zero iff zero, unit norm, mutual orthogonality (the projection is Gram-Schmidt orthonormalized).
	TEST(GroupTheoryBridgeTests, ProjectsFourSymmetryAdaptedVectorsWithExactAndNumericForms)
	{
		const GroupTheoryBridge bridge;
		const Result<PointGroupReduction> result = bridge.ReduceRepresentation(MakeNvFourBondBasis());
		if (!result)
			GTEST_SKIP() << "groupy unavailable in current environment: " << result.Error().technicalDetails;

		ASSERT_EQ(result->projectedVectors.size(), 4u);

		int a1Count = 0;
		int eRow0Count = 0;
		int eRow1Count = 0;
		for (const SymmetryAdaptedVector &vector : result->projectedVectors)
		{
			ASSERT_EQ(vector.coefficients.size(), 4u) << "one coefficient per basis site expected";

			double normSquared = 0.0;
			for (const ExactCoefficient &coefficient : vector.coefficients)
			{
				EXPECT_FALSE(coefficient.exact.empty()) << "exact form must never be dropped";
				const bool exactIsZero = coefficient.exact == "0";
				EXPECT_EQ(exactIsZero, coefficient.numeric == 0.0)
					<< "exact '" << coefficient.exact << "' disagrees with numeric " << coefficient.numeric;
				normSquared += coefficient.numeric * coefficient.numeric;
			}
			EXPECT_NEAR(normSquared, 1.0, 1e-9) << "projected vectors are expected orthonormal";

			if (vector.irrepLabel == "A1")
			{
				++a1Count;
				EXPECT_EQ(vector.irrepRow, 0);
			}
			else if (vector.irrepLabel == "E")
			{
				EXPECT_TRUE(vector.irrepRow == 0 || vector.irrepRow == 1);
				(vector.irrepRow == 0 ? eRow0Count : eRow1Count) += 1;
			}
			else
			{
				ADD_FAILURE() << "unexpected irrep in projection: " << vector.irrepLabel;
			}
		}
		EXPECT_EQ(a1Count, 2);
		EXPECT_EQ(eRow0Count, 1);
		EXPECT_EQ(eRow1Count, 1);

		// The two A1 copies are distinguished only by occurrenceIndex, so those must differ.
		std::vector<int> a1Occurrences;
		for (const SymmetryAdaptedVector &vector : result->projectedVectors)
		{
			if (vector.irrepLabel == "A1")
				a1Occurrences.push_back(vector.occurrenceIndex);
		}
		ASSERT_EQ(a1Occurrences.size(), 2u);
		EXPECT_NE(a1Occurrences[0], a1Occurrences[1]);

		for (std::size_t i = 0; i < result->projectedVectors.size(); ++i)
		{
			for (std::size_t j = i + 1; j < result->projectedVectors.size(); ++j)
				EXPECT_NEAR(Dot(result->projectedVectors[i], result->projectedVectors[j]), 0.0, 1e-9);
		}
	}

	// Acceptance criterion 3: a bad point-group label is a StructuredError, not an exception and not
	// a crash. groupy raises on an unknown name; the bridge has to catch that at the subprocess
	// boundary.
	TEST(GroupTheoryBridgeTests, ReportsStructuredErrorForUnknownPointGroup)
	{
		PermutationRepresentationRequest request = MakeNvFourBondBasis();
		request.pointGroupLabel = "NotAPointGroup";

		const GroupTheoryBridge bridge;
		const Result<PointGroupReduction> result = bridge.ReduceRepresentation(request);

		ASSERT_FALSE(result) << "an unknown point group must not produce a result";
		EXPECT_FALSE(result.Error().code.empty());
		EXPECT_FALSE(result.Error().userMessage.empty());
	}

	// The basis has to be closed under the group. Moving one carbon off the three-fold orbit means
	// a rotated site lands nowhere near a real site, and that must be rejected rather than quietly
	// snapped to the nearest one.
	TEST(GroupTheoryBridgeTests, RejectsBasisNotClosedUnderTheGroup)
	{
		PermutationRepresentationRequest request = MakeNvFourBondBasis();
		request.sites[2].position = glm::dvec3(0.37, 0.11, 0.0);

		const GroupTheoryBridge bridge;
		const Result<PointGroupReduction> result = bridge.ReduceRepresentation(request);

		ASSERT_FALSE(result) << "a basis the group does not permute must be rejected";
		EXPECT_FALSE(result.Error().code.empty());
	}

	TEST(GroupTheoryBridgeTests, ReportsStructuredErrorForEmptyBasis)
	{
		PermutationRepresentationRequest request;
		request.pointGroupLabel = "C3v";

		const GroupTheoryBridge bridge;
		const Result<PointGroupReduction> result = bridge.ReduceRepresentation(request);

		ASSERT_FALSE(result) << "an empty basis has no representation to reduce";
		EXPECT_FALSE(result.Error().code.empty());
	}
} // namespace DefectStudio::Tests
