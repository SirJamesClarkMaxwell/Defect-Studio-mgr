#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace DefectStudio
{
	struct VtCell
	{
		char32_t codepoint = U' ';
		uint8_t foreground = 7; // ANSI color index, 0-15
		uint8_t background = 0;
		bool bold = false;
	};

	// Minimal ANSI/VT100 screen buffer: turns a raw ConPTY output byte stream into a character
	// grid, enough to render an interactive remote shell - cursor movement, line/screen clear,
	// basic 16-color SGR, line wrap, scroll-on-linefeed. A plain scrollback of printed text
	// (the pre-ConPTY MVP) can't show this: readline redraws the prompt in place using cursor
	// moves, not by re-printing lines.
	// ponytail: no 256-color/truecolor SGR, no alternate screen buffer (vim/htop/less will
	// render wrong), no OSC title parsing (swallowed, not shown), byte-level text (not real
	// UTF-8 decoding - multi-byte characters render as several mojibake cells). Upgrade path:
	// a real VT100/xterm conformance suite if a full-screen TUI app needs to run inside this
	// panel; until then this covers what bash/PSReadLine emit for ordinary line editing.
	class VtScreen
	{
	public:
		explicit VtScreen(int columns = 80, int rows = 24);

		void Resize(int columns, int rows);
		void Feed(std::string_view bytes);

		[[nodiscard]] int Columns() const
		{
			return m_Columns;
		}

		[[nodiscard]] int Rows() const
		{
			return m_Rows;
		}

		[[nodiscard]] int CursorColumn() const
		{
			return m_CursorColumn;
		}

		[[nodiscard]] int CursorRow() const
		{
			return m_CursorRow;
		}

		[[nodiscard]] const VtCell &CellAt(int row, int column) const;

		// Rows that scrolled off the top, oldest first. Unbounded (MVP - see class comment).
		[[nodiscard]] const std::vector<std::vector<VtCell>> &Scrollback() const
		{
			return m_Scrollback;
		}

	private:
		void feedByte(unsigned char byte);
		void putChar(char32_t ch);
		void lineFeed();
		void carriageReturn();
		void backspace();
		void tab();
		void eraseInLine(int mode);
		void eraseInDisplay(int mode);
		void cursorMove(int deltaRow, int deltaColumn);
		void cursorTo(int row, int column);
		void insertBlank(int count);
		void deleteChars(int count);
		void applySgr();
		void handleCsiFinal(char finalByte);
		void newLineScroll();
		void clampCursor();
		void pushParamDigit(unsigned char digit);
		void endParam();

		int m_Columns;
		int m_Rows;
		int m_CursorRow = 0;
		int m_CursorColumn = 0;
		uint8_t m_CurrentForeground = 7;
		uint8_t m_CurrentBackground = 0;
		bool m_CurrentBold = false;

		std::vector<std::vector<VtCell>> m_Grid;
		std::vector<std::vector<VtCell>> m_Scrollback;

		// Parser state persists across Feed() calls - ConPTY output can split an escape
		// sequence across two reads.
		enum class ParseState
		{
			Ground,
			Escape,
			Csi,
			Osc
		};
		ParseState m_State = ParseState::Ground;
		std::vector<int> m_Params;
		int m_CurrentParam = -1;
		bool m_ParamQuestion = false;
	};
} // namespace DefectStudio
