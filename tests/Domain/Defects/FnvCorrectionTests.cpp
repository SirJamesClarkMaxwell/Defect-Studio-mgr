#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>

#include "Domain/Defects/FnvCorrection.hpp"
#include "../../ScientificRuntime/ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr double kBohr = 0.529177210903;

		// sxdefectalign's vline-eV-aN.dat: block 1 = (z bohr, V_model), '&', block 2 = (z, dV, dV - V_model).
		[[nodiscard]] FnvAxisProfile ReadVline(const std::string &name)
		{
			std::ifstream file(ScientificRuntimeFixturePath("fnv/GeN_q-1/" + name).Native());
			FnvAxisProfile axis;
			std::string line;
			bool secondBlock = false;
			while (std::getline(file, line))
			{
				if (line.find('&') != std::string::npos)
				{
					secondBlock = true;
					continue;
				}
				std::istringstream row(line);
				double z = 0.0, a = 0.0;
				if (!(row >> z >> a))
					continue;
				if (secondBlock)
					axis.deltaV.push_back(a);
				else
				{
					axis.position.push_back(z * kBohr);
					axis.modelPotential.push_back(a);
				}
			}
			if (axis.position.size() > 1)
				axis.length = axis.position[1] * static_cast<double>(axis.position.size());
			return axis;
		}

		// GeN q = -1 from the BSc thesis, sxdefectalign pass 1 (no -C), E_lat 0.252685 eV. The
		// profiles are already in the sxdefectalign convention, so they go in as "pymatgen" with a
		// zero mean: no shift.
		[[nodiscard]] FnvProfiles GeNProfiles()
		{
			FnvProfiles profiles;
			profiles.charge = -1;
			profiles.dielectric = 5.7;
			profiles.latticeEnergy = 0.252685;
			for (int a = 0; a < 3; ++a)
				profiles.axes[a] = ReadVline("vline-eV-a" + std::to_string(a) + ".dat");
			return profiles;
		}

		// A 10 A axis, 100 points: a Gaussian model potential at x = 3 and a DFT difference equal to
		// it plus `offset` everywhere outside the core.
		[[nodiscard]] FnvAxisProfile SyntheticAxis(const double offset, const double slope = 1.0)
		{
			FnvAxisProfile axis;
			axis.length = 10.0;
			for (int i = 0; i < 100; ++i)
			{
				const double x = 0.1 * i;
				double d = std::abs(x - 3.0);
				d = std::min(d, 10.0 - d);
				const double model = -0.2 * std::exp(-d * d / 2.0);
				axis.position.push_back(x);
				axis.modelPotential.push_back(model);
				axis.deltaV.push_back(slope * model + offset);
			}
			return axis;
		}
	} // namespace

	TEST(FnvCorrectionTests, ReproducesTheThesisPlateauOnGeN)
	{
		const FnvProfiles profiles = GeNProfiles();
		ASSERT_EQ(profiles.axes[0].position.size(), 180u);

		struct Expected { double defect, width; bool flat; double alignment, spread, slope; };
		// From scripts/python/fnv_sxdefectalign.py propose_window / model_slope on the same files.
		const std::array<Expected, 3> expected = {{
			{6.54001385717598, 2.825866972704061, true, 0.02795597142857143, 0.00985612317433965, 1.0227885914834132},
			{7.958330115358511, 2.116708843612, false, 0.012493185185185184, 0.015030970294786145, 1.1261138391581018},
			{6.54001385717598, 2.825866972704061, true, 0.027985057142857143, 0.009826735013487752, 1.0224706260826606},
		}};
		std::array<FnvAxisAlignment, 3> alignments;
		for (int a = 0; a < 3; ++a)
		{
			SCOPED_TRACE(a);
			alignments[a] = ProposeFnvAlignment(profiles.axes[a], FnvModelPotential(profiles, a, FnvModelConvention::Pymatgen));
			EXPECT_NEAR(alignments[a].defectPosition, expected[a].defect, 1e-6);
			EXPECT_NEAR(alignments[a].window.width, expected[a].width, 1e-6);
			EXPECT_EQ(alignments[a].flat, expected[a].flat);
			EXPECT_NEAR(alignments[a].alignment, expected[a].alignment, 2e-6); // vline residual column is rounded
			EXPECT_NEAR(alignments[a].spread, expected[a].spread, 2e-6);
			EXPECT_NEAR(alignments[a].slope, expected[a].slope, 1e-4);
			// The window sits opposite the defect.
			const double opposite = std::fmod(expected[a].defect + 0.5 * profiles.axes[a].length, profiles.axes[a].length);
			EXPECT_NEAR(alignments[a].window.centre, opposite, 1e-6);
		}

		const FnvCorrection correction = CombineFnvCorrection(profiles, alignments);
		EXPECT_NEAR(correction.alignment, 0.02281140458553792, 2e-6);
		EXPECT_NEAR(correction.correction, 0.22987359541446206, 2e-6); // E_lat + q C, q = -1
		EXPECT_NEAR(correction.alignmentSpread, 0.01549187195767196, 2e-6);
		EXPECT_DOUBLE_EQ(correction.latticeEnergy, 0.252685);
	}

	TEST(FnvCorrectionTests, TheSxdefectalignConventionShiftsTheModelByEpsilonMinusOneTimesItsMean)
	{
		FnvProfiles profiles;
		profiles.dielectric = 5.7;
		profiles.modelPotentialMean = -0.00078;
		profiles.axes[0] = SyntheticAxis(0.0);
		const std::vector<double> pymatgen = FnvModelPotential(profiles, 0, FnvModelConvention::Pymatgen);
		const std::vector<double> sx = FnvModelPotential(profiles, 0, FnvModelConvention::Sxdefectalign);
		ASSERT_EQ(pymatgen, profiles.axes[0].modelPotential);
		ASSERT_EQ(sx.size(), pymatgen.size());
		for (std::size_t i = 0; i < sx.size(); ++i)
			EXPECT_NEAR(sx[i] - pymatgen[i], 4.7 * -0.00078, 1e-12);
	}

	TEST(FnvCorrectionTests, AFlatResidualIsFoundAndItsOffsetIsTheAlignment)
	{
		const FnvAxisProfile axis = SyntheticAxis(0.03);
		const FnvAxisAlignment alignment = ProposeFnvAlignment(axis, axis.modelPotential);
		EXPECT_NEAR(alignment.defectPosition, 3.0, 1e-9);
		EXPECT_NEAR(alignment.window.centre, 8.0, 1e-9);
		EXPECT_TRUE(alignment.flat);
		EXPECT_NEAR(alignment.alignment, 0.03, 1e-12);
		EXPECT_NEAR(alignment.spread, 0.0, 1e-12);
		EXPECT_NEAR(alignment.slope, 1.0, 1e-9);
		// Perfectly flat everywhere: the widest allowed window, half the axis.
		EXPECT_NEAR(alignment.window.width, 5.0, 0.1);
	}

	TEST(FnvCorrectionTests, ADelocalisedChargeHasNoSlope)
	{
		// dV ignores the model entirely - the Ge_C +-1 picture.
		const FnvAxisProfile axis = SyntheticAxis(0.02, 0.0);
		EXPECT_NEAR(ProposeFnvAlignment(axis, axis.modelPotential).slope, 0.0, 1e-9);
	}

	TEST(FnvCorrectionTests, AWavyResidualHasNoPlateauAndFallsBackToTheMinimumWidth)
	{
		FnvAxisProfile axis = SyntheticAxis(0.0);
		for (std::size_t i = 0; i < axis.deltaV.size(); ++i)
			axis.deltaV[i] += 0.05 * std::sin(2.0 * std::numbers::pi * axis.position[i] / 2.5); // spread >> tolerance
		const FnvAxisAlignment alignment = ProposeFnvAlignment(axis, axis.modelPotential);
		EXPECT_FALSE(alignment.flat);
		EXPECT_NEAR(alignment.window.width, kFnvMinimumPlateauWidth, 1e-9);
		EXPECT_NEAR(alignment.window.centre, 8.0, 1e-9);
	}

	TEST(FnvCorrectionTests, AUserWindowWrapsAroundTheCellEdge)
	{
		FnvAxisProfile axis = SyntheticAxis(0.0);
		// Residual 0.01 near x = 0 / 10 (within 1 A of the edge), 0 elsewhere.
		for (std::size_t i = 0; i < axis.deltaV.size(); ++i)
			if (axis.position[i] <= 1.0 + 1e-9 || axis.position[i] >= 9.0 - 1e-9)
				axis.deltaV[i] += 0.01;
		const FnvAxisAlignment edge = AlignFnvInWindow(axis, axis.modelPotential, {0.0, 1.0});
		EXPECT_NEAR(edge.alignment, 0.01, 1e-12); // points 9.5..9.9 and 0.0..0.5
		EXPECT_NEAR(edge.window.centre, 0.0, 1e-12);
		EXPECT_TRUE(edge.flat);

		const FnvAxisAlignment empty = AlignFnvInWindow(axis, axis.modelPotential, {5.05, 0.02});
		EXPECT_FALSE(empty.flat);
		EXPECT_DOUBLE_EQ(empty.alignment, 0.0);
	}

	TEST(FnvCorrectionTests, OnlyTheChosenAxesEnterTheCorrection)
	{
		FnvProfiles profiles;
		profiles.charge = 2;
		profiles.latticeEnergy = 1.0;
		std::array<FnvAxisAlignment, 3> alignments;
		alignments[0].alignment = 0.01;
		alignments[1].alignment = 0.04;
		alignments[2].alignment = 0.02;

		const FnvCorrection all = CombineFnvCorrection(profiles, alignments);
		EXPECT_NEAR(all.alignment, 0.07 / 3.0, 1e-12);
		EXPECT_NEAR(all.alignmentSpread, 0.03, 1e-12);
		EXPECT_NEAR(all.correction, 1.0 + 2.0 * 0.07 / 3.0, 1e-12);

		const FnvCorrection twoAxes = CombineFnvCorrection(profiles, alignments, {true, false, true});
		EXPECT_NEAR(twoAxes.alignment, 0.015, 1e-12);
		EXPECT_NEAR(twoAxes.correction, 1.03, 1e-12);

		const FnvCorrection none = CombineFnvCorrection(profiles, alignments, {false, false, false});
		EXPECT_NEAR(none.alignment, all.alignment, 1e-12);
	}
} // namespace DefectStudio::Tests
