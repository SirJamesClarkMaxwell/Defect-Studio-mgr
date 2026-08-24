#include "Core/dspch.hpp"

#include "Presentation/Terminal/VtScreen.hpp"

#include <algorithm>

namespace DefectStudio
{
	namespace
	{
		std::vector<VtCell> MakeBlankRow(int columns)
		{
			return std::vector<VtCell>(static_cast<std::size_t>(columns));
		}

		int ParamOr(const std::vector<int> &params, std::size_t index, int fallback)
		{
			if (index >= params.size())
				return fallback;
			return params[index] == 0 ? fallback : params[index];
		}

		int ParamOrZero(const std::vector<int> &params, std::size_t index)
		{
			return index < params.size() ? params[index] : 0;
		}
	} // namespace

	VtScreen::VtScreen(int columns, int rows)
		: m_Columns(std::max(columns, 1)), m_Rows(std::max(rows, 1))
	{
		m_Grid.assign(static_cast<std::size_t>(m_Rows), MakeBlankRow(m_Columns));
	}

	void VtScreen::Resize(int columns, int rows)
	{
		columns = std::max(columns, 1);
		rows = std::max(rows, 1);
		if (columns == m_Columns && rows == m_Rows)
			return;

		std::vector<std::vector<VtCell>> resized(static_cast<std::size_t>(rows), MakeBlankRow(columns));
		const int rowsToCopy = std::min(rows, m_Rows);
		const int colsToCopy = std::min(columns, m_Columns);
		for (int row = 0; row < rowsToCopy; ++row)
			for (int col = 0; col < colsToCopy; ++col)
				resized[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] =
					m_Grid[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];

		m_Grid = std::move(resized);
		m_Columns = columns;
		m_Rows = rows;
		clampCursor();
	}

	const VtCell &VtScreen::CellAt(int row, int column) const
	{
		static const VtCell blank{};
		if (row < 0 || row >= m_Rows || column < 0 || column >= m_Columns)
			return blank;
		return m_Grid[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
	}

	void VtScreen::clampCursor()
	{
		m_CursorRow = std::clamp(m_CursorRow, 0, m_Rows - 1);
		m_CursorColumn = std::clamp(m_CursorColumn, 0, m_Columns - 1);
	}

	void VtScreen::Feed(std::string_view bytes)
	{
		for (const char rawByte : bytes)
			feedByte(static_cast<unsigned char>(rawByte));
	}

	void VtScreen::feedByte(unsigned char byte)
	{
		switch (m_State)
		{
			case ParseState::Ground:
				if (byte == 0x1B)
				{
					m_State = ParseState::Escape;
				}
				else if (byte == '\r')
				{
					carriageReturn();
				}
				else if (byte == '\n')
				{
					lineFeed();
				}
				else if (byte == '\b')
				{
					backspace();
				}
				else if (byte == '\t')
				{
					tab();
				}
				else if (byte == 0x07)
				{
					// Bell - no visual effect in this MVP.
				}
				else if (byte >= 0x20 && byte != 0x7F)
				{
					putChar(static_cast<char32_t>(byte));
				}
				break;

			case ParseState::Escape:
				if (byte == '[')
				{
					m_State = ParseState::Csi;
					m_Params.clear();
					m_CurrentParam = -1;
					m_ParamQuestion = false;
				}
				else if (byte == ']')
				{
					m_State = ParseState::Osc;
				}
				else
				{
					// Unimplemented single-byte escape (e.g. reset, reverse-index) - consume
					// and drop rather than leaking it into the visible grid.
					m_State = ParseState::Ground;
				}
				break;

			case ParseState::Csi:
				if (byte >= '0' && byte <= '9')
				{
					pushParamDigit(byte);
				}
				else if (byte == ';' || byte == ':')
				{
					endParam();
				}
				else if (byte == '?')
				{
					m_ParamQuestion = true;
				}
				else if (byte >= 0x40 && byte <= 0x7E)
				{
					endParam();
					handleCsiFinal(static_cast<char>(byte));
					m_State = ParseState::Ground;
				}
				// Other intermediate bytes (e.g. space, '!') are silently ignored.
				break;

			case ParseState::Osc:
				// OSC (window title, ...) - swallow until BEL. ST (ESC \) termination is not
				// specially handled (rare in practice here); the ESC that starts it just
				// re-enters Escape state, ending the OSC.
				if (byte == 0x07)
					m_State = ParseState::Ground;
				else if (byte == 0x1B)
					m_State = ParseState::Escape;
				break;
		}
	}

	void VtScreen::pushParamDigit(unsigned char digit)
	{
		if (m_CurrentParam < 0)
			m_CurrentParam = 0;
		m_CurrentParam = m_CurrentParam * 10 + (digit - '0');
	}

	void VtScreen::endParam()
	{
		m_Params.push_back(m_CurrentParam < 0 ? 0 : m_CurrentParam);
		m_CurrentParam = -1;
	}

	void VtScreen::putChar(char32_t ch)
	{
		m_Grid[static_cast<std::size_t>(m_CursorRow)][static_cast<std::size_t>(m_CursorColumn)] =
			VtCell{ch, m_CurrentForeground, m_CurrentBackground, m_CurrentBold};
		++m_CursorColumn;
		if (m_CursorColumn >= m_Columns)
		{
			m_CursorColumn = 0;
			lineFeed();
		}
	}

	void VtScreen::newLineScroll()
	{
		m_Scrollback.push_back(std::move(m_Grid.front()));
		m_Grid.erase(m_Grid.begin());
		m_Grid.push_back(MakeBlankRow(m_Columns));
	}

	void VtScreen::lineFeed()
	{
		++m_CursorRow;
		if (m_CursorRow >= m_Rows)
		{
			newLineScroll();
			m_CursorRow = m_Rows - 1;
		}
	}

	void VtScreen::carriageReturn()
	{
		m_CursorColumn = 0;
	}

	void VtScreen::backspace()
	{
		if (m_CursorColumn > 0)
			--m_CursorColumn;
	}

	void VtScreen::tab()
	{
		const int nextStop = ((m_CursorColumn / 8) + 1) * 8;
		m_CursorColumn = std::min(nextStop, m_Columns - 1);
	}

	void VtScreen::cursorMove(int deltaRow, int deltaColumn)
	{
		m_CursorRow = std::clamp(m_CursorRow + deltaRow, 0, m_Rows - 1);
		m_CursorColumn = std::clamp(m_CursorColumn + deltaColumn, 0, m_Columns - 1);
	}

	void VtScreen::cursorTo(int row, int column)
	{
		m_CursorRow = std::clamp(row, 0, m_Rows - 1);
		m_CursorColumn = std::clamp(column, 0, m_Columns - 1);
	}

	void VtScreen::eraseInLine(int mode)
	{
		auto &row = m_Grid[static_cast<std::size_t>(m_CursorRow)];
		const int from = mode == 1 ? 0 : m_CursorColumn;
		const int to = mode == 0 ? m_Columns - 1 : (mode == 1 ? m_CursorColumn : m_Columns - 1);
		for (int col = from; col <= to; ++col)
			row[static_cast<std::size_t>(col)] = VtCell{};
	}

	void VtScreen::eraseInDisplay(int mode)
	{
		if (mode == 2 || mode == 3)
		{
			for (auto &row : m_Grid)
				std::fill(row.begin(), row.end(), VtCell{});
			return;
		}

		if (mode == 0)
		{
			eraseInLine(0);
			for (int row = m_CursorRow + 1; row < m_Rows; ++row)
				std::fill(m_Grid[static_cast<std::size_t>(row)].begin(), m_Grid[static_cast<std::size_t>(row)].end(), VtCell{});
		}
		else if (mode == 1)
		{
			for (int row = 0; row < m_CursorRow; ++row)
				std::fill(m_Grid[static_cast<std::size_t>(row)].begin(), m_Grid[static_cast<std::size_t>(row)].end(), VtCell{});
			eraseInLine(1);
		}
	}

	void VtScreen::insertBlank(int count)
	{
		auto &row = m_Grid[static_cast<std::size_t>(m_CursorRow)];
		count = std::clamp(count, 0, m_Columns - m_CursorColumn);
		for (int col = m_Columns - 1; col >= m_CursorColumn + count; --col)
			row[static_cast<std::size_t>(col)] = row[static_cast<std::size_t>(col - count)];
		for (int col = m_CursorColumn; col < m_CursorColumn + count; ++col)
			row[static_cast<std::size_t>(col)] = VtCell{};
	}

	void VtScreen::deleteChars(int count)
	{
		auto &row = m_Grid[static_cast<std::size_t>(m_CursorRow)];
		count = std::clamp(count, 0, m_Columns - m_CursorColumn);
		for (int col = m_CursorColumn; col < m_Columns - count; ++col)
			row[static_cast<std::size_t>(col)] = row[static_cast<std::size_t>(col + count)];
		for (int col = m_Columns - count; col < m_Columns; ++col)
			row[static_cast<std::size_t>(col)] = VtCell{};
	}

	void VtScreen::applySgr()
	{
		if (m_Params.empty())
		{
			m_CurrentForeground = 7;
			m_CurrentBackground = 0;
			m_CurrentBold = false;
			return;
		}

		for (const int code : m_Params)
		{
			if (code == 0)
			{
				m_CurrentForeground = 7;
				m_CurrentBackground = 0;
				m_CurrentBold = false;
			}
			else if (code == 1)
				m_CurrentBold = true;
			else if (code == 22)
				m_CurrentBold = false;
			else if (code >= 30 && code <= 37)
				m_CurrentForeground = static_cast<uint8_t>(code - 30);
			else if (code == 39)
				m_CurrentForeground = 7;
			else if (code >= 40 && code <= 47)
				m_CurrentBackground = static_cast<uint8_t>(code - 40);
			else if (code == 49)
				m_CurrentBackground = 0;
			else if (code >= 90 && code <= 97)
				m_CurrentForeground = static_cast<uint8_t>(code - 90 + 8);
			else if (code >= 100 && code <= 107)
				m_CurrentBackground = static_cast<uint8_t>(code - 100 + 8);
			// Everything else (256-color/truecolor introducers, underline, blink, ...) is a
			// deliberate no-op - see class comment.
		}
	}

	void VtScreen::handleCsiFinal(char finalByte)
	{
		switch (finalByte)
		{
			case 'A':
				cursorMove(-ParamOr(m_Params, 0, 1), 0);
				break;
			case 'B':
				cursorMove(ParamOr(m_Params, 0, 1), 0);
				break;
			case 'C':
				cursorMove(0, ParamOr(m_Params, 0, 1));
				break;
			case 'D':
				cursorMove(0, -ParamOr(m_Params, 0, 1));
				break;
			case 'H':
			case 'f':
				cursorTo(ParamOr(m_Params, 0, 1) - 1, ParamOr(m_Params, 1, 1) - 1);
				break;
			case 'J':
				eraseInDisplay(ParamOrZero(m_Params, 0));
				break;
			case 'K':
				eraseInLine(ParamOrZero(m_Params, 0));
				break;
			case 'm':
				applySgr();
				break;
			case '@':
				insertBlank(ParamOr(m_Params, 0, 1));
				break;
			case 'P':
				deleteChars(ParamOr(m_Params, 0, 1));
				break;
			default:
				// Scroll region, mode set/reset (?h/?l), and everything else this MVP doesn't
				// implement - deliberate no-op, see class comment.
				break;
		}
	}
} // namespace DefectStudio
