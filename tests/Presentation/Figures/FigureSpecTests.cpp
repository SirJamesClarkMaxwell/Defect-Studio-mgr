#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <regex>
#include <nlohmann/json.hpp>

#include "Presentation/Figures/FigureSpec.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/TextFileIO.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"
#include "ScientificRuntime/ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		FigureSpec TwoSeries()
		{
			FigureSpec spec;
			spec.name = "S_fixture";
			spec.formats = {"pdf", "svg", "png"};
			spec.createdBy = "DefectStudio 0.1.0";
			spec.plot.id = "S"; spec.plot.title = "S_k i S(hw)";
			spec.plot.x = {"\\hbar\\omega", "meV", 0, 100};
			spec.plot.y = {"S_k", "", 0, 2};
			spec.plot.y2 = {"S(\\hbar\\omega)", "meV^{-1}", 0.01, 1, true};
			spec.plot.hasY2 = true;
			PlotSeriesData stems;
			stems.id = "S/A1"; stems.label = "A1"; stems.kind = PlotSeriesKind::Stems;
			stems.x = {10, 20}; stems.y = {1, 2}; stems.source = Path::FromUtf8("results/żółty");
			stems.style.color = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
			stems.style.visible = false; stems.style.lineWeight = 2.5f; stems.style.lineStyle = 3;
			PlotSeriesData spectrum;
			spectrum.id = "S/spectrum"; spectrum.label = "spectrum"; spectrum.kind = PlotSeriesKind::Segments;
			spectrum.x = {10, 20, std::numeric_limits<double>::quiet_NaN(), 30};
			spectrum.y = {0.1, 0.2, std::numeric_limits<double>::quiet_NaN(), 0.3};
			spectrum.yAxis = 1; spectrum.style.filled = true; spectrum.style.fillAlpha = 0.5f;
			spec.plot.series = {stems, spectrum};
			return spec;
		}
	}

	TEST(FigureSpecTests, TwoSeriesRightAxisAndStylesRoundtrip)
	{
		const auto original = TwoSeries();
		const auto json = SerializeFigureSpec(original);
		FigureSpec parsed;
		std::string error = "previous error";
		ASSERT_TRUE(ParseFigureSpec(json, parsed, error)) << error;
		EXPECT_TRUE(error.empty());
		EXPECT_EQ(SerializeFigureSpec(parsed), json);
		EXPECT_EQ(parsed.name, original.name);
		EXPECT_DOUBLE_EQ(parsed.widthCm, 8.5); EXPECT_DOUBLE_EQ(parsed.heightCm, 6.5);
		EXPECT_EQ(parsed.formats, original.formats); EXPECT_EQ(parsed.dpi, 300);
		EXPECT_EQ(parsed.createdBy, original.createdBy);
		EXPECT_TRUE(parsed.plot.hasY2); EXPECT_TRUE(parsed.plot.y2.log);
		ASSERT_EQ(parsed.plot.series.size(), 2u);
		const auto &stems = parsed.plot.series[0];
		EXPECT_EQ(stems.kind, PlotSeriesKind::Stems); EXPECT_FALSE(stems.style.visible);
		ASSERT_TRUE(stems.style.color); EXPECT_EQ(*stems.style.color, *original.plot.series[0].style.color);
		EXPECT_EQ(stems.source.Utf8(), original.plot.series[0].source.Utf8());
		EXPECT_FLOAT_EQ(stems.style.lineWeight, 2.5f); EXPECT_EQ(stems.style.lineStyle, 3);
		EXPECT_EQ(parsed.plot.series[1].yAxis, 1); EXPECT_TRUE(parsed.plot.series[1].style.filled);
		EXPECT_FLOAT_EQ(parsed.plot.series[1].style.fillAlpha, 0.5f);
		EXPECT_FALSE(parsed.plot.series[1].style.color);
	}

	TEST(FigureSpecTests, NonFiniteSamplesAreNullAndBecomeNanWithoutLosingGaps)
	{
		auto spec = TwoSeries();
		spec.plot.series[1].y[3] = std::numeric_limits<double>::infinity();
		const auto json = nlohmann::json::parse(SerializeFigureSpec(spec));
		EXPECT_TRUE(json["plot"]["series"][1]["x"][2].is_null());
		EXPECT_TRUE(json["plot"]["series"][1]["y"][2].is_null());
		EXPECT_TRUE(json["plot"]["series"][1]["y"][3].is_null());
		FigureSpec parsed;
		std::string error;
		ASSERT_TRUE(ParseFigureSpec(json.dump(), parsed, error)) << error;
		EXPECT_TRUE(std::isnan(parsed.plot.series[1].x[2]));
		EXPECT_TRUE(std::isnan(parsed.plot.series[1].y[2]));
		EXPECT_TRUE(std::isnan(parsed.plot.series[1].y[3]));
		EXPECT_EQ(parsed.plot.series[1].x.size(), 4u);
	}

	TEST(FigureSpecTests, InvalidVersionAndMalformedJsonLeaveOutputUnchanged)
	{
		FigureSpec parsed = TwoSeries();
		const auto original = SerializeFigureSpec(parsed);
		auto json = nlohmann::json::parse(original);
		json["schemaVersion"] = 2;
		std::string error;
		EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error));
		EXPECT_NE(error.find("schemaVersion"), std::string::npos);
		EXPECT_EQ(SerializeFigureSpec(parsed), original);
		json["schemaVersion"] = 1.5;
		EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error));
		EXPECT_FALSE(ParseFigureSpec("{broken", parsed, error));
		EXPECT_EQ(SerializeFigureSpec(parsed), original);
	}

	TEST(FigureSpecTests, RejectsUnsafeNamesSizesFormatsAndSeries)
	{
		const auto original = nlohmann::json::parse(SerializeFigureSpec(TwoSeries()));
		FigureSpec parsed;
		std::string error;
		for (const auto &name : {"../escape", "folder/name", "CON", "name.", ""})
		{
			auto json = original; json["name"] = name;
			EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error)) << name;
		}
		for (const auto &change : std::vector<nlohmann::json>{
			{{"widthCm", 0}}, {{"heightCm", nullptr}}, {{"dpi", -1}}, {{"formats", {"jpg"}}},
			{{"formats", nlohmann::json::array()}}, {{"style", "../style"}}})
		{
			auto json = original; json.update(change);
			EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error)) << change.dump();
		}
		auto json = original; json["plot"]["series"][1]["y"] = {1};
		EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error));
		json = original; json["plot"]["hasY2"] = false;
		EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error));
		json = original; json["plot"]["series"][0]["style"]["color"] = {0, 0, 0, 2};
		EXPECT_FALSE(ParseFigureSpec(json.dump(), parsed, error));
	}

	TEST(FigureSpecTests, AllKindsAndMarkersRoundtripIncludingVerticalLineWithoutY)
	{
		auto spec = TwoSeries();
		spec.plot.series.clear();
		for (const auto kind : {PlotSeriesKind::Line, PlotSeriesKind::Stems, PlotSeriesKind::Scatter,
			PlotSeriesKind::VerticalLine, PlotSeriesKind::Segments})
		{
			PlotSeriesData series;
			series.id = std::to_string(static_cast<int>(kind)); series.kind = kind; series.x = {1};
			if (kind != PlotSeriesKind::VerticalLine) series.y = {2};
			series.marker.shape = PlotMarker::Shape::Diamond; series.marker.size = 7; series.marker.filled = false;
			spec.plot.series.push_back(series);
		}
		FigureSpec parsed;
		std::string error;
		ASSERT_TRUE(ParseFigureSpec(SerializeFigureSpec(spec), parsed, error)) << error;
		EXPECT_EQ(SerializeFigureSpec(parsed), SerializeFigureSpec(spec));
		EXPECT_TRUE(parsed.plot.series[3].y.empty());
		EXPECT_EQ(parsed.plot.series[2].marker.shape, PlotMarker::Shape::Diamond);
		EXPECT_FALSE(parsed.plot.series[2].marker.filled);
	}

	TEST(FigureSpecTests, ScriptRunnerRendersSelfTest)
	{
		ScriptRunner runner;
		ScriptRunOptions options;
		options.scriptPath = PythonExamplePath("render_figure.py");
		options.arguments = {"--self-test"}; options.workingDirectory = FindRepoRoot();
		options.requireZeroExitCode = false;
		const auto result = runner.RunFile(options);
		if (!result)
		{
			GTEST_SKIP() << "Python script unavailable in local environment: " << result.Error().technicalDetails;
		}
		ASSERT_TRUE(result);
		const auto json = nlohmann::json::parse(ExtractJsonLineFromOutput(result->standardOutput));
		if (!json.at("ok").get<bool>() && json.value("error", "").find("No module named 'matplotlib'") != std::string::npos)
			GTEST_SKIP() << "matplotlib unavailable: " << json.at("error");
		EXPECT_EQ(result->exitCode, 0);
		EXPECT_TRUE(json.at("ok").get<bool>());
	}

	TEST(FigureSpecTests, RendererAcceptsCppSnapshotAndKeepsPdfPageSize)
	{
		struct Workspace
		{
			Path root = Path::FromResolved(FileSystem::TempDirectoryPath()) / ("ds_figure_" + ToString(GenerateUuid()));
			~Workspace() { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		} workspace;
		const auto spec = TwoSeries();
		const auto path = workspace.root.Join(spec.name + ".figure.json");
		std::string error;
		ASSERT_TRUE(TextFileIO::Save(path, SerializeFigureSpec(spec), error)) << error;
		ScriptRunner runner;
		ScriptRunOptions options;
		options.scriptPath = PythonExamplePath("render_figure.py");
		options.arguments = {path.Utf8()}; options.workingDirectory = FindRepoRoot();
		options.requireZeroExitCode = false;
		const auto result = runner.RunFile(options);
		if (!result) GTEST_SKIP() << "Python script unavailable in local environment: " << result.Error().technicalDetails;
		const auto json = nlohmann::json::parse(ExtractJsonLineFromOutput(result->standardOutput));
		if (!json.at("ok").get<bool>() && json.value("error", "").find("No module named 'matplotlib'") != std::string::npos)
			GTEST_SKIP() << "matplotlib unavailable: " << json.at("error");
		ASSERT_EQ(result->exitCode, 0) << result->standardOutput << result->standardError;
		ASSERT_TRUE(json.at("ok").get<bool>());
		EXPECT_EQ(json.at("outputs").size(), 3u);
		for (const auto &format : spec.formats) EXPECT_TRUE(FileSystem::Exists(workspace.root.Join(spec.name + "." + format).Native()));
		std::string pdf;
		ASSERT_TRUE(TextFileIO::Load(workspace.root.Join(spec.name + ".pdf"), pdf, error)) << error;
		const std::regex mediaBox(R"(/MediaBox\s*\[\s*0\s+0\s+([\d.]+)\s+([\d.]+)\s*\])");
		std::smatch page;
		ASSERT_TRUE(std::regex_search(pdf, page, mediaBox));
		EXPECT_NEAR(std::stod(page[1].str()), spec.widthCm / 2.54 * 72, 1e-6);
		EXPECT_NEAR(std::stod(page[2].str()), spec.heightCm / 2.54 * 72, 1e-6);
	}
}
