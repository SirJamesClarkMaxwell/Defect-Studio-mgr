#include <gtest/gtest.h>

#include "Presentation/Terminal/VtScreen.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		std::string RowText(const VtScreen &screen, int row)
		{
			std::string text;
			for (int col = 0; col < screen.Columns(); ++col)
				text.push_back(static_cast<char>(screen.CellAt(row, col).codepoint));
			return text;
		}
	} // namespace

	TEST(VtScreenTests, PlainTextAdvancesCursor)
	{
		VtScreen screen(10, 5);
		screen.Feed("hi");

		EXPECT_EQ(screen.CellAt(0, 0).codepoint, U'h');
		EXPECT_EQ(screen.CellAt(0, 1).codepoint, U'i');
		EXPECT_EQ(screen.CursorRow(), 0);
		EXPECT_EQ(screen.CursorColumn(), 2);
	}

	TEST(VtScreenTests, CarriageReturnLineFeedMovesToNextLineStart)
	{
		VtScreen screen(10, 5);
		screen.Feed("ab\r\ncd");

		EXPECT_EQ(RowText(screen, 0).substr(0, 2), "ab");
		EXPECT_EQ(RowText(screen, 1).substr(0, 2), "cd");
		EXPECT_EQ(screen.CursorRow(), 1);
		EXPECT_EQ(screen.CursorColumn(), 2);
	}

	TEST(VtScreenTests, LineWrapsAtLastColumn)
	{
		VtScreen screen(3, 5);
		screen.Feed("abcd");

		EXPECT_EQ(RowText(screen, 0), "abc");
		EXPECT_EQ(screen.CellAt(1, 0).codepoint, U'd');
		EXPECT_EQ(screen.CursorRow(), 1);
		EXPECT_EQ(screen.CursorColumn(), 1);
	}

	TEST(VtScreenTests, LineFeedAtBottomRowScrollsIntoScrollback)
	{
		VtScreen screen(10, 2);
		screen.Feed("line1\r\nline2\r\nline3");

		ASSERT_EQ(screen.Scrollback().size(), 1u);
		EXPECT_EQ(static_cast<char>(screen.Scrollback().front()[0].codepoint), 'l');
		EXPECT_EQ(RowText(screen, 0).substr(0, 5), "line2");
		EXPECT_EQ(RowText(screen, 1).substr(0, 5), "line3");
	}

	TEST(VtScreenTests, CursorPositionEscapeMovesCursor)
	{
		VtScreen screen(10, 5);
		screen.Feed("\x1b[3;4Hx");

		EXPECT_EQ(screen.CellAt(2, 3).codepoint, U'x'); // 1-indexed in the escape, 0-indexed here
	}

	TEST(VtScreenTests, CursorForwardBackMovesWithinLine)
	{
		VtScreen screen(10, 5);
		screen.Feed("abc\x1b[2Dx");

		// Cursor was at column 3 after "abc", CSI 2D moves it back to column 1.
		EXPECT_EQ(screen.CellAt(0, 1).codepoint, U'x');
	}

	TEST(VtScreenTests, EraseInLineClearsFromCursorToEnd)
	{
		VtScreen screen(10, 5);
		screen.Feed("abcdef\x1b[3D\x1b[K");

		EXPECT_EQ(RowText(screen, 0).substr(0, 3), "abc");
		EXPECT_EQ(screen.CellAt(0, 3).codepoint, U' ');
		EXPECT_EQ(screen.CellAt(0, 5).codepoint, U' ');
	}

	TEST(VtScreenTests, EraseDisplayModeTwoClearsEverything)
	{
		VtScreen screen(5, 2);
		screen.Feed("abcde\r\nfghij\x1b[2J");

		for (int row = 0; row < screen.Rows(); ++row)
			for (int col = 0; col < screen.Columns(); ++col)
				EXPECT_EQ(screen.CellAt(row, col).codepoint, U' ');
	}

	TEST(VtScreenTests, SgrSetsForegroundColorUntilReset)
	{
		VtScreen screen(10, 5);
		screen.Feed("\x1b[31mred\x1b[0mplain");

		EXPECT_EQ(screen.CellAt(0, 0).foreground, 1); // ANSI red = index 1
		EXPECT_EQ(screen.CellAt(0, 3).foreground, 7); // reset back to default
	}

	TEST(VtScreenTests, BoldSgrPersistsAcrossCharacters)
	{
		VtScreen screen(10, 5);
		screen.Feed("\x1b[1mbold");

		EXPECT_TRUE(screen.CellAt(0, 0).bold);
		EXPECT_TRUE(screen.CellAt(0, 3).bold);
	}

	TEST(VtScreenTests, UnknownEscapeSequenceIsSwallowedNotPrinted)
	{
		VtScreen screen(10, 5);
		// Bracketed-paste mode toggle - unimplemented, must not leak into the grid as text.
		screen.Feed("\x1b[?2004h" "ok");

		EXPECT_EQ(RowText(screen, 0).substr(0, 2), "ok");
	}

	TEST(VtScreenTests, TabAdvancesToNextEightColumnStop)
	{
		VtScreen screen(20, 5);
		screen.Feed("a\tb");

		EXPECT_EQ(screen.CellAt(0, 0).codepoint, U'a');
		EXPECT_EQ(screen.CellAt(0, 8).codepoint, U'b');
	}

	TEST(VtScreenTests, ResizePreservesTopLeftContent)
	{
		VtScreen screen(10, 5);
		screen.Feed("hello");
		screen.Resize(20, 10);

		EXPECT_EQ(screen.Columns(), 20);
		EXPECT_EQ(screen.Rows(), 10);
		EXPECT_EQ(RowText(screen, 0).substr(0, 5), "hello");
	}

	TEST(VtScreenTests, SplitEscapeSequenceAcrossFeedCallsStillParses)
	{
		VtScreen screen(10, 5);
		screen.Feed("\x1b[3");
		screen.Feed("1mred");

		EXPECT_EQ(screen.CellAt(0, 0).foreground, 1);
	}
} // namespace DefectStudio::Tests
