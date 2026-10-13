#include "Core/dspch.hpp"
#include "IO/DataTableIO.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string Cell(std::string text, char delimiter)
		{
			if (delimiter == ',')
			{
				if (text.find_first_of(",\"\r\n") == std::string::npos)
					return text;
				std::string quoted = "\"";
				for (const char character : text)
					quoted += character == '"' ? std::string("\"\"") : std::string(1, character);
				return quoted + "\"";
			}
			std::replace_if(text.begin(), text.end(), [&](char c) { return c == delimiter || c == '\n' || c == '\r'; }, ' ');
			return text;
		}

		std::size_t Rows(const DataTableColumn &column)
		{
			return column.text.empty() ? column.values.size() : column.text.size();
		}
	} // namespace

	std::string FormatDataTable(std::span<const DataTableColumn> columns, const DataTableFormat &format)
	{
		std::string out;
		const auto line = [&](auto &&field) {
			for (std::size_t i = 0; i < columns.size(); ++i)
			{
				if (i)
					out += format.delimiter;
				out += Cell(field(columns[i]), format.delimiter);
			}
			out += '\n';
		};
		line([](const DataTableColumn &column) { return column.name; });
		if (format.originHeader)
		{
			line([](const DataTableColumn &column) { return column.unit; });
			line([](const DataTableColumn &column) { return column.comment; });
		}
		std::size_t rows = 0;
		for (const auto &column : columns)
			rows = std::max(rows, Rows(column));
		for (std::size_t row = 0; row < rows; ++row)
			line([&](const DataTableColumn &column) -> std::string {
				if (!column.text.empty())
					return row < column.text.size() ? column.text[row] : std::string();
				if (row >= column.values.size() || !std::isfinite(column.values[row]))
					return {};
				// std::format without 'L' is locale independent: always '.'.
				return std::format("{:.{}g}", column.values[row], format.significantDigits);
			});
		return out;
	}

	bool SaveDataTable(const Path &file, std::span<const DataTableColumn> columns, const DataTableFormat &format,
		std::string &outError)
	{
		return TextFileIO::Save(file, FormatDataTable(columns, format), outError);
	}
} // namespace DefectStudio
