#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Domain/Symmetry/SymmetryAdaptedBasis.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] std::vector<ExactCoefficient> Coefficients(std::size_t count, double imaginary = 0.0)
		{
			return std::vector<ExactCoefficient>(count, ExactCoefficient{"1/2", 0.5, imaginary, "\\frac{1}{2}"});
		}

		[[nodiscard]] SymmetryAdaptedVector Vector(
			const std::string &irrep, int occurrence, int row, std::size_t sites = 4, double imaginary = 0.0)
		{
			return SymmetryAdaptedVector{irrep, occurrence, row, Coefficients(sites, imaginary), ""};
		}

		// NV- dangling bonds: 2 A1 + E over N + 3 C.
		[[nodiscard]] PointGroupReduction MakeNv()
		{
			PointGroupReduction reduction;
			reduction.pointGroupLabel = "C3v";
			reduction.siteLabels = {"N0", "C1", "C2", "C3"};
			reduction.groupOrder = 6;
			reduction.decomposition = {{"A1", 2, 1}, {"E", 1, 2}};
			reduction.projectedVectors = {Vector("A1", 0, 0), Vector("A1", 1, 0), Vector("E", 0, 0), Vector("E", 0, 1)};
			return reduction;
		}

		// A 6-site orbit with E twice: the case the row-major order of task 53 got wrong.
		[[nodiscard]] PointGroupReduction MakeDoubleE()
		{
			PointGroupReduction reduction;
			reduction.pointGroupLabel = "C3v";
			reduction.siteLabels = {"a", "b", "c", "d", "e", "f"};
			reduction.groupOrder = 6;
			reduction.decomposition = {{"A1", 1, 1}, {"A2", 1, 1}, {"E", 2, 2}};
			reduction.projectedVectors = {Vector("A1", 0, 0, 6), Vector("A2", 0, 0, 6), Vector("E", 0, 0, 6),
				Vector("E", 0, 1, 6), Vector("E", 1, 0, 6), Vector("E", 1, 1, 6)};
			return reduction;
		}

		// C3 triangle: A + E+ + E-, with the real basis of the pair.
		[[nodiscard]] PointGroupReduction MakeC3()
		{
			PointGroupReduction reduction;
			reduction.pointGroupLabel = "C3";
			reduction.siteLabels = {"C0", "C1", "C2"};
			reduction.groupOrder = 3;
			reduction.decomposition = {{"A", 1, 1}, {"E+", 1, 1}, {"E-", 1, 1}};
			reduction.projectedVectors = {Vector("A", 0, 0, 3), Vector("E+", 0, 0, 3, 0.5), Vector("E-", 0, 0, 3, -0.5)};
			SymmetryAdaptedVector u = Vector("E+", 0, 0, 3);
			u.conjugateIrrepLabel = "E-";
			SymmetryAdaptedVector v = Vector("E+", 0, 1, 3);
			v.conjugateIrrepLabel = "E-";
			reduction.realPairVectors = {u, v};
			return reduction;
		}

		void ExpectError(const PointGroupReduction &reduction, const std::string &code)
		{
			const Result<void> result = ValidateSymmetryAdaptedBasis(reduction);
			ASSERT_FALSE(result) << "expected " << code;
			EXPECT_EQ(result.Error().code, code);
			EXPECT_EQ(result.Error().category, ErrorCategory::Validation);
		}
	} // namespace

	TEST(SymmetryAdaptedBasisTests, AcceptsWellFormedRealReduction)
	{
		EXPECT_TRUE(ValidateSymmetryAdaptedBasis(MakeNv()));
		EXPECT_TRUE(ValidateSymmetryAdaptedBasis(MakeDoubleE()));
	}

	TEST(SymmetryAdaptedBasisTests, AcceptsWellFormedReductionWithRealPairs)
	{
		EXPECT_TRUE(ValidateSymmetryAdaptedBasis(MakeC3()));
	}

	TEST(SymmetryAdaptedBasisTests, AcceptsEmptyReduction)
	{
		PointGroupReduction reduction;
		reduction.siteLabels = {"a"};
		EXPECT_TRUE(ValidateSymmetryAdaptedBasis(reduction));
	}

	TEST(SymmetryAdaptedBasisTests, RejectsUnknownIrrep)
	{
		PointGroupReduction reduction = MakeNv();
		reduction.projectedVectors.push_back(Vector("A2", 0, 0));
		ExpectError(reduction, "symmetry.salc.unknown_irrep");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsMissingRow)
	{
		PointGroupReduction reduction = MakeNv();
		reduction.projectedVectors.pop_back(); // E row 1 gone
		ExpectError(reduction, "symmetry.salc.incomplete_copy");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsDuplicateRow)
	{
		PointGroupReduction reduction = MakeNv();
		reduction.projectedVectors[3].irrepRow = 0; // E row 0 twice, row 1 missing
		ExpectError(reduction, "symmetry.salc.incomplete_copy");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsOccurrenceGap)
	{
		PointGroupReduction reduction = MakeNv();
		reduction.projectedVectors[1].occurrenceIndex = 2; // A1 copies 0 and 2, multiplicity 2
		ExpectError(reduction, "symmetry.salc.incomplete_copy");
	}

	// Task 53's script listed row 0 of every copy, then row 1 of every copy.
	TEST(SymmetryAdaptedBasisTests, RejectsRowMajorOrder)
	{
		PointGroupReduction reduction = MakeDoubleE();
		reduction.projectedVectors = {Vector("A1", 0, 0, 6), Vector("A2", 0, 0, 6), Vector("E", 0, 0, 6),
			Vector("E", 1, 0, 6), Vector("E", 0, 1, 6), Vector("E", 1, 1, 6)};
		ExpectError(reduction, "symmetry.salc.order");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsIrrepsOutOfDecompositionOrder)
	{
		PointGroupReduction reduction = MakeNv();
		reduction.projectedVectors = {Vector("E", 0, 0), Vector("E", 0, 1), Vector("A1", 0, 0), Vector("A1", 1, 0)};
		ExpectError(reduction, "symmetry.salc.order");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsWrongCoefficientCount)
	{
		PointGroupReduction projected = MakeNv();
		projected.projectedVectors[2].coefficients.pop_back();
		ExpectError(projected, "symmetry.salc.coefficient_count");

		PointGroupReduction pair = MakeC3();
		pair.realPairVectors[1].coefficients.push_back(ExactCoefficient{"0", 0.0, 0.0, "0"});
		ExpectError(pair, "symmetry.salc.coefficient_count");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsRealPairWithImaginaryPart)
	{
		PointGroupReduction reduction = MakeC3();
		reduction.realPairVectors[0].coefficients[1].numericImaginary = 1e-6;
		ExpectError(reduction, "symmetry.salc.real_pair");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsRealPairWithoutConjugateLabel)
	{
		PointGroupReduction reduction = MakeC3();
		reduction.realPairVectors[0].conjugateIrrepLabel.clear();
		ExpectError(reduction, "symmetry.salc.real_pair");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsRealPairWithUnknownConjugate)
	{
		PointGroupReduction reduction = MakeC3();
		for (auto &vector : reduction.realPairVectors)
			vector.conjugateIrrepLabel = "E2-";
		ExpectError(reduction, "symmetry.salc.real_pair");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsRealPairMissingComponent)
	{
		PointGroupReduction reduction = MakeC3();
		reduction.realPairVectors.pop_back(); // v gone
		ExpectError(reduction, "symmetry.salc.real_pair");
	}

	TEST(SymmetryAdaptedBasisTests, RejectsRealPairWithUnequalMultiplicity)
	{
		PointGroupReduction reduction = MakeC3();
		reduction.decomposition[2].multiplicity = 2;
		reduction.projectedVectors.push_back(Vector("E-", 1, 0, 3, -0.5));
		ExpectError(reduction, "symmetry.salc.real_pair");
	}
} // namespace DefectStudio::Tests
