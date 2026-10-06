#include <gtest/gtest.h>

#include "Renderer/Text/TexMarkup.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] std::u32string Codepoints(const std::vector<TexGlyph> &glyphs)
		{
			std::u32string text;
			for (const TexGlyph &glyph : glyphs)
				text.push_back(glyph.codepoint);
			return text;
		}

		void ExpectGlyph(const TexGlyph &glyph, char32_t codepoint, float scale, float shift)
		{
			EXPECT_EQ(glyph.codepoint, codepoint);
			EXPECT_NEAR(glyph.scale, scale, 1e-5f);
			EXPECT_NEAR(glyph.baselineShift, shift, 1e-5f);
		}
	} // namespace

	TEST(TexMarkupTests, PlainTextIsOneNormalGlyphPerCharacter)
	{
		const auto glyphs = ParseTexMarkup("V_");
		ASSERT_EQ(glyphs.size(), 2u);
		ExpectGlyph(glyphs[0], U'V', 1.0f, 0.0f);
		ExpectGlyph(glyphs[1], U'_', 1.0f, 0.0f); // trailing '_' is literal
		EXPECT_TRUE(ParseTexMarkup("").empty());
		EXPECT_EQ(Codepoints(ParseTexMarkup("NV center")), U"NV center");
	}

	TEST(TexMarkupTests, Utf8IsDecoded)
	{
		EXPECT_EQ(Codepoints(ParseTexMarkup("1.54 \xC3\x85")), U"1.54 Å");
		EXPECT_EQ(Codepoints(ParseTexMarkup("\xCE\xB1")), U"α");
	}

	TEST(TexMarkupTests, SubscriptTakesOneCharacterOrAGroup)
	{
		const auto single = ParseTexMarkup("V_BN");
		ASSERT_EQ(single.size(), 3u);
		ExpectGlyph(single[0], U'V', 1.0f, 0.0f);
		ExpectGlyph(single[1], U'B', kTexScriptScale, kTexSubscriptShift);
		ExpectGlyph(single[2], U'N', 1.0f, 0.0f);

		const auto group = ParseTexMarkup("V_{Si}");
		ASSERT_EQ(group.size(), 3u);
		ExpectGlyph(group[1], U'S', kTexScriptScale, kTexSubscriptShift);
		ExpectGlyph(group[2], U'i', kTexScriptScale, kTexSubscriptShift);
	}

	TEST(TexMarkupTests, SuperscriptsNestAndMultiplyTheScale)
	{
		const auto glyphs = ParseTexMarkup("e^{x_1}");
		ASSERT_EQ(glyphs.size(), 3u);
		ExpectGlyph(glyphs[1], U'x', kTexScriptScale, kTexSuperscriptShift);
		ExpectGlyph(glyphs[2], U'1', kTexScriptScale * kTexScriptScale,
			kTexSuperscriptShift + kTexSubscriptShift * kTexScriptScale);
	}

	TEST(TexMarkupTests, CommandsBecomeSymbols)
	{
		EXPECT_EQ(Codepoints(ParseTexMarkup("\\alpha\\Gamma\\omega")), U"αΓω");
		EXPECT_EQ(Codepoints(ParseTexMarkup("\\Alpha")), U"A");
		EXPECT_EQ(Codepoints(ParseTexMarkup("1.5\\,\\AA")), U"1.5 Å");
		EXPECT_EQ(Codepoints(ParseTexMarkup("\\pm\\to\\deg\\cdot")), U"±→°·");
		EXPECT_EQ(Codepoints(ParseTexMarkup("a\\ b")), U"a b");
		// A command ends at the first non-letter, so it can touch the next word's punctuation.
		EXPECT_EQ(Codepoints(ParseTexMarkup("\\alpha2")), U"α2");
	}

	TEST(TexMarkupTests, UnknownCommandsAndEscapesAreLiteral)
	{
		EXPECT_EQ(Codepoints(ParseTexMarkup("\\foo")), U"\\foo");
		EXPECT_EQ(Codepoints(ParseTexMarkup("\\_\\^\\{\\}\\\\")), U"_^{}\\");
		EXPECT_EQ(Codepoints(ParseTexMarkup("{ab}")), U"ab");
		EXPECT_EQ(Codepoints(ParseTexMarkup("a}")), U"a}");
	}

	TEST(TexMarkupTests, OpenGroupRunsToTheEnd)
	{
		const auto glyphs = ParseTexMarkup("x^{ab");
		ASSERT_EQ(glyphs.size(), 3u);
		ExpectGlyph(glyphs[2], U'b', kTexScriptScale, kTexSuperscriptShift);
	}

	TEST(TexMarkupTests, ScriptCommandAndMinusSign)
	{
		const auto greek = ParseTexMarkup("E_\\sigma");
		ASSERT_EQ(greek.size(), 2u);
		ExpectGlyph(greek[1], U'σ', kTexScriptScale, kTexSubscriptShift);

		const auto charge = ParseTexMarkup("V_N^-");
		ASSERT_EQ(charge.size(), 3u);
		ExpectGlyph(charge[2], U'−', kTexScriptScale, kTexSuperscriptShift);
		EXPECT_EQ(Codepoints(ParseTexMarkup("a-b")), U"a-b"); // only inside scripts
	}

	TEST(TexMarkupTests, OppositeScriptOnTheSameBaseStacks)
	{
		const auto glyphs = ParseTexMarkup("E_g^{(1)}");
		ASSERT_EQ(glyphs.size(), 5u);
		EXPECT_FALSE(glyphs[1].stacksOnPreviousScript);
		EXPECT_TRUE(glyphs[2].stacksOnPreviousScript);
		EXPECT_FALSE(glyphs[3].stacksOnPreviousScript);
		// Two subscripts in a row, or a script after plain text, do not stack.
		EXPECT_FALSE(ParseTexMarkup("a_bc^d")[3].stacksOnPreviousScript);
		EXPECT_FALSE(ParseTexMarkup("a_b_c")[2].stacksOnPreviousScript);
	}

	TEST(TexMarkupTests, LayoutAdvancesByScaledAdvance)
	{
		const auto advance = [](char32_t) { return 0.5f; };
		const TexLayout plain = LayoutTexGlyphs(ParseTexMarkup("ab"), advance);
		ASSERT_EQ(plain.glyphs.size(), 2u);
		EXPECT_FLOAT_EQ(plain.glyphs[0].x, 0.0f);
		EXPECT_FLOAT_EQ(plain.glyphs[1].x, 0.5f);
		EXPECT_FLOAT_EQ(plain.width, 1.0f);

		const TexLayout script = LayoutTexGlyphs(ParseTexMarkup("V_B"), advance);
		EXPECT_FLOAT_EQ(script.glyphs[1].x, 0.5f);
		EXPECT_FLOAT_EQ(script.glyphs[1].scale, kTexScriptScale);
		EXPECT_FLOAT_EQ(script.glyphs[1].baselineShift, kTexSubscriptShift);
		EXPECT_FLOAT_EQ(script.width, 0.5f + 0.5f * kTexScriptScale);
	}

	TEST(TexMarkupTests, StackedScriptStartsUnderThePreviousOneAndWidthIsTheLonger)
	{
		const auto advance = [](char32_t) { return 1.0f; };
		// E (1) + g (0.7) stacked with "(1)" (3 * 0.7 = 2.1): width = 1 + 2.1.
		const TexLayout layout = LayoutTexGlyphs(ParseTexMarkup("E_g^{(1)}"), advance);
		ASSERT_EQ(layout.glyphs.size(), 5u);
		EXPECT_FLOAT_EQ(layout.glyphs[1].x, 1.0f);
		EXPECT_FLOAT_EQ(layout.glyphs[2].x, 1.0f);
		EXPECT_FLOAT_EQ(layout.glyphs[4].x, 1.0f + 2.0f * kTexScriptScale);
		EXPECT_FLOAT_EQ(layout.width, 1.0f + 3.0f * kTexScriptScale);

		// Text after a stack continues from the longer script.
		const TexLayout after = LayoutTexGlyphs(ParseTexMarkup("E_{gg}^1x"), advance);
		ASSERT_EQ(after.glyphs.size(), 5u);
		EXPECT_FLOAT_EQ(after.glyphs[4].x, 1.0f + 2.0f * kTexScriptScale);
	}
} // namespace DefectStudio::Tests
