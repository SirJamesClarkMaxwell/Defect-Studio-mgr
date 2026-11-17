#include "Core/dspch.hpp"
#include "IO/DataTableIO.hpp"

#include <algorithm>
#include <charconv>
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

		std::string_view Trim(std::string_view text)
		{
			const auto first = text.find_first_not_of(" \t\r\v\f");
			if (first == std::string_view::npos)
				return {};
			return text.substr(first, text.find_last_not_of(" \t\r\v\f") - first + 1);
		}

		char Delimiter(std::string_view line)
		{
			for (const char delimiter : {'\t', ';', ','})
				if (line.find(delimiter) != std::string_view::npos)
					return delimiter;
			return ' ';
		}

		std::vector<std::string_view> Fields(std::string_view line, char delimiter)
		{
			std::vector<std::string_view> fields;
			while (true)
			{
				if (delimiter == ' ')
				{
					line = Trim(line);
					if (line.empty())
						break;
				}
				const auto end = delimiter == ' ' ? line.find_first_of(" \t") : line.find(delimiter);
				fields.push_back(Trim(line.substr(0, end)));
				if (end == std::string_view::npos)
					break;
				line.remove_prefix(end + 1);
			}
			return fields;
		}

		bool Number(std::string_view field, char delimiter, double &value)
		{
			std::string number(field);
			if (delimiter != ',' && number.find('.') == std::string::npos &&
				std::count(number.begin(), number.end(), ',') == 1)
				std::replace(number.begin(), number.end(), ',', '.');
			field = number;
			if (!field.empty() && field.front() == '+')
			{
				field.remove_prefix(1);
				if (!field.empty() && (field.front() == '-' || field.front() == '+'))
					return false;
			}
			if (field.empty())
				return false;
			const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
			return parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size() && std::isfinite(value);
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

	bool ParseDataTable(std::string_view text, ParsedDataTable &out, std::string &outError)
	{
		outError.clear();
		if (text.starts_with("\xEF\xBB\xBF"))
			text.remove_prefix(3);
		ParsedDataTable result;
		std::vector<std::string_view> headerLines;
		while (!text.empty())
		{
			const auto end = text.find('\n');
			auto line = text.substr(0, end);
			text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			const auto trimmed = Trim(line);
			if (trimmed.empty())
			{
				if (result.columns.empty() && !line.empty())
					headerLines.push_back(line);
				continue;
			}
			if (trimmed.front() == '#')
			{
				++result.skippedLines;
				continue;
			}
			const char delimiter = result.columns.empty() ? Delimiter(line) : result.delimiter;
			const auto fields = Fields(line, delimiter);
			std::vector<double> values(fields.size());
			bool valid = fields.size() >= 2 && (result.columns.empty() || fields.size() == result.columns.size());
			for (std::size_t i = 0; valid && i < fields.size(); ++i)
				valid = Number(fields[i], delimiter, values[i]);
			if (!valid)
			{
				++result.skippedLines;
				if (result.columns.empty())
					headerLines.push_back(line);
				continue;
			}
			if (result.columns.empty())
			{
				result.delimiter = delimiter;
				result.columns.resize(fields.size());
				result.names.resize(fields.size());
				for (std::size_t i = headerLines.size(); i > 0; --i)
				{
					auto names = Fields(headerLines[i - 1], delimiter);
					if (names.size() != fields.size())
						continue;
					if (i >= 3 && Fields(headerLines[i - 2], delimiter).size() == fields.size() &&
						Fields(headerLines[i - 3], delimiter).size() == fields.size())
						names = Fields(headerLines[i - 3], delimiter);
					for (std::size_t column = 0; column < names.size(); ++column)
						result.names[column] = names[column];
					break;
				}
			}
			for (std::size_t i = 0; i < values.size(); ++i)
				result.columns[i].push_back(values[i]);
		}
		if (result.columns.empty())
		{
			outError = "No numeric data row with at least two columns was found";
			return false;
		}
		out = std::move(result);
		return true;
	}

	bool LoadDataTable(const Path &file, ParsedDataTable &out, std::string &outError)
	{
		std::string text;
		if (!TextFileIO::Load(file, text, outError))
			return false;
		return ParseDataTable(text, out, outError);
	}
} // namespace DefectStudio
