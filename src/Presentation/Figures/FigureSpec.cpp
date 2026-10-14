#include "Core/dspch.hpp"
#include "Presentation/Figures/FigureSpec.hpp"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace DefectStudio
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::array<std::string_view, 5> Kinds{"Line", "Stems", "Scatter", "VerticalLine", "Segments"};

		Json Number(double value)
		{
			return std::isfinite(value) ? Json(value) : Json(nullptr);
		}

		double ReadNumber(const Json &value)
		{
			if (value.is_null()) return std::numeric_limits<double>::quiet_NaN();
			const auto result = value.get<double>();
			if (!std::isfinite(result)) throw std::runtime_error("Non-finite JSON number");
			return result;
		}

		int ReadInteger(const Json &value)
		{
			if (!value.is_number_integer()) throw std::runtime_error("Expected an integer");
			const auto result = value.get<std::int64_t>();
			if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
				throw std::runtime_error("Integer out of range");
			return static_cast<int>(result);
		}

		void CheckStem(const std::string &value)
		{
			if (value.empty() || value == "." || value == ".." || value.back() == '.' || value.back() == ' ' ||
				std::any_of(value.begin(), value.end(), [](unsigned char ch) {
					return ch < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos;
				})) throw std::runtime_error("Name and style must be file stems without path separators");
			std::string base = value.substr(0, value.find('.'));
			for (auto &ch : base) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
				(base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' && base[3] <= '9'))
				throw std::runtime_error("Reserved file name");
		}

		Json Axis(const PlotAxisData &axis)
		{
			return {{"labelTex", axis.labelTex}, {"unit", axis.unit}, {"minimum", Number(axis.minimum)},
				{"maximum", Number(axis.maximum)}, {"log", axis.log}};
		}

		PlotAxisData ReadAxis(const Json &value)
		{
			PlotAxisData axis{value.at("labelTex").get<std::string>(), value.at("unit").get<std::string>(),
				ReadNumber(value.at("minimum")), ReadNumber(value.at("maximum")), value.at("log").get<bool>()};
			if (std::isfinite(axis.minimum) && std::isfinite(axis.maximum) && axis.minimum < axis.maximum &&
				axis.log && axis.minimum <= 0) throw std::runtime_error("Log axis limits must be positive");
			return axis;
		}

		Json Samples(const std::vector<double> &values)
		{
			Json result = Json::array();
			for (const auto value : values) result.push_back(Number(value));
			return result;
		}

		std::vector<double> ReadSamples(const Json &values)
		{
			if (!values.is_array()) throw std::runtime_error("Samples must be arrays");
			std::vector<double> result;
			result.reserve(values.size());
			for (const auto &value : values) result.push_back(ReadNumber(value));
			return result;
		}
	}

	std::string SerializeFigureSpec(const FigureSpec &spec)
	{
		Json series = Json::array();
		for (const auto &item : spec.plot.series)
		{
			Json color = nullptr;
			if (item.style.color)
			{
				color = Json::array();
				for (int i = 0; i < 4; ++i) color.push_back(Number((*item.style.color)[i]));
			}
			series.push_back({{"id", item.id}, {"label", item.label}, {"kind", Kinds.at(static_cast<std::size_t>(item.kind))},
				{"x", Samples(item.x)}, {"y", Samples(item.y)}, {"yAxis", item.yAxis}, {"source", item.source.Utf8()},
				{"style", {{"color", color}, {"weight", Number(item.style.lineWeight)}, {"visible", item.style.visible},
					{"lineStyle", item.style.lineStyle}, {"filled", item.style.filled}, {"fillAlpha", Number(item.style.fillAlpha)}}},
				{"marker", {{"shape", static_cast<int>(item.marker.shape)}, {"size", Number(item.marker.size)}, {"filled", item.marker.filled}}}});
		}
		return Json{{"schemaVersion", spec.schemaVersion}, {"name", spec.name}, {"widthCm", Number(spec.widthCm)},
			{"heightCm", Number(spec.heightCm)}, {"style", spec.style}, {"formats", spec.formats}, {"dpi", spec.dpi},
			{"createdBy", spec.createdBy}, {"plot", {{"id", spec.plot.id}, {"title", spec.plot.title},
				{"x", Axis(spec.plot.x)}, {"y", Axis(spec.plot.y)}, {"y2", Axis(spec.plot.y2)},
				{"hasY2", spec.plot.hasY2}, {"series", series}}}}.dump(2) + "\n";
	}

	bool ParseFigureSpec(const std::string &json, FigureSpec &out, std::string &outError)
	{
		try
		{
			const auto value = Json::parse(json);
			FigureSpec spec;
			spec.schemaVersion = ReadInteger(value.at("schemaVersion"));
			if (spec.schemaVersion != 1) throw std::runtime_error("Unsupported FigureSpec schemaVersion (expected 1)");
			spec.name = value.at("name").get<std::string>();
			spec.style = value.at("style").get<std::string>();
			CheckStem(spec.name); CheckStem(spec.style);
			spec.widthCm = ReadNumber(value.at("widthCm")); spec.heightCm = ReadNumber(value.at("heightCm"));
			spec.dpi = ReadInteger(value.at("dpi"));
			if (!(spec.widthCm > 0) || !(spec.heightCm > 0) || spec.dpi <= 0)
				throw std::runtime_error("Figure size and DPI must be positive");
			spec.formats = value.at("formats").get<std::vector<std::string>>();
			if (spec.formats.empty()) throw std::runtime_error("Select at least one output format");
			for (const auto &format : spec.formats)
				if (format != "pdf" && format != "svg" && format != "png") throw std::runtime_error("Unsupported output format");
			spec.createdBy = value.at("createdBy").get<std::string>();
			const auto &plot = value.at("plot");
			spec.plot.id = plot.at("id").get<std::string>(); spec.plot.title = plot.at("title").get<std::string>();
			spec.plot.x = ReadAxis(plot.at("x")); spec.plot.y = ReadAxis(plot.at("y")); spec.plot.y2 = ReadAxis(plot.at("y2"));
			spec.plot.hasY2 = plot.at("hasY2").get<bool>();
			if (!plot.at("series").is_array()) throw std::runtime_error("Series must be an array");
			for (const auto &item : plot.at("series"))
			{
				PlotSeriesData series;
				series.id = item.at("id").get<std::string>(); series.label = item.at("label").get<std::string>();
				const auto kind = item.at("kind").get<std::string>();
				const auto found = std::find(Kinds.begin(), Kinds.end(), kind);
				if (found == Kinds.end()) throw std::runtime_error("Unsupported series kind");
				series.kind = static_cast<PlotSeriesKind>(found - Kinds.begin());
				series.x = ReadSamples(item.at("x")); series.y = ReadSamples(item.at("y"));
				if (series.kind != PlotSeriesKind::VerticalLine && series.x.size() != series.y.size())
					throw std::runtime_error("Series x/y lengths differ");
				series.yAxis = ReadInteger(item.at("yAxis"));
				if (series.yAxis < 0 || series.yAxis > 1 || (series.yAxis == 1 && !spec.plot.hasY2))
					throw std::runtime_error("Invalid series yAxis");
				series.source = Path::FromUtf8(item.at("source").get<std::string>());
				const auto &style = item.at("style");
				if (!style.at("color").is_null())
				{
					const auto &color = style.at("color");
					if (!color.is_array() || color.size() != 4) throw std::runtime_error("Color must have four components");
					glm::vec4 rgba;
					for (int i = 0; i < 4; ++i)
					{
						const auto component = ReadNumber(color.at(i));
						if (!(component >= 0 && component <= 1)) throw std::runtime_error("Color out of range");
						rgba[i] = static_cast<float>(component);
					}
					series.style.color = rgba;
				}
				series.style.lineWeight = static_cast<float>(ReadNumber(style.at("weight")));
				series.style.visible = style.at("visible").get<bool>(); series.style.filled = style.at("filled").get<bool>();
				series.style.fillAlpha = static_cast<float>(ReadNumber(style.at("fillAlpha")));
				series.style.lineStyle = ReadInteger(style.at("lineStyle"));
				if (!(series.style.lineWeight > 0) || !std::isfinite(series.style.lineWeight) ||
					!(series.style.fillAlpha >= 0 && series.style.fillAlpha <= 1) || series.style.lineStyle < 0 || series.style.lineStyle > 3)
					throw std::runtime_error("Invalid series style");
				if (item.contains("marker"))
				{
					const auto &marker = item.at("marker");
					const auto shape = ReadInteger(marker.at("shape"));
					series.marker.size = static_cast<float>(ReadNumber(marker.at("size")));
					if (shape < 0 || shape > 2 || !(series.marker.size > 0) || !std::isfinite(series.marker.size))
						throw std::runtime_error("Invalid marker");
					series.marker.shape = static_cast<PlotMarker::Shape>(shape);
					series.marker.filled = marker.at("filled").get<bool>();
				}
				spec.plot.series.push_back(std::move(series));
			}
			out = std::move(spec);
			outError.clear();
			return true;
		}
		catch (const std::exception &error)
		{
			outError = error.what();
			return false;
		}
	}
}
