#include <gtest/gtest.h>

#include "Renderer/AtomStyleTable.hpp"

#include <unordered_map>

namespace DefectStudio::Tests
{
	TEST(AtomStyleTableTests, ReplacedStylesProvideKnownValues)
	{
		AtomStyleTable table;
		std::unordered_map<std::string, AtomRenderStyle> styles;
		styles.emplace("N", AtomRenderStyle{glm::vec3(0.19f, 0.31f, 0.97f), 0.33f});
		table.ReplaceStyles(std::move(styles), VacancyRenderStyle{});
		ASSERT_EQ(table.Size(), 1u);

		const AtomRenderStyle &nitrogen = table.GetStyle("N");
		EXPECT_NEAR(nitrogen.color.x, 0.19f, 1e-3f);
		EXPECT_NEAR(nitrogen.color.y, 0.31f, 1e-3f);
		EXPECT_NEAR(nitrogen.color.z, 0.97f, 1e-3f);
		EXPECT_NEAR(nitrogen.displayRadius, 0.33f, 1e-3f);
	}

	TEST(AtomStyleTableTests, ProvidesVacancyGhostStyle)
	{
		AtomStyleTable table;
		std::unordered_map<std::string, AtomRenderStyle> styles;
		VacancyRenderStyle vacancyStyle;
		vacancyStyle.opacity = 0.35f;
		vacancyStyle.renderMode = VacancyRenderMode::Ghost;
		vacancyStyle.displayRadius = 0.45f;
		table.ReplaceStyles(std::move(styles), vacancyStyle);

		const VacancyRenderStyle &vacancy = table.GetVacancyStyle();
		EXPECT_NEAR(vacancy.opacity, 0.35f, 1e-3f);
		EXPECT_EQ(vacancy.renderMode, VacancyRenderMode::Ghost);
		EXPECT_NEAR(vacancy.displayRadius, 0.45f, 1e-3f);
	}

	TEST(AtomStyleTableTests, CopiesShareUnderlyingDataAfterReplaceStyles)
	{
		// Deliberate: a copy taken before a later ReplaceStyles must still see it. Renderer commands
		// are handed a copy of AtomStyleTable at registration time (RendererCommandRegistration.cpp),
		// long before any UI can edit it - without this sharing, ElementCatalogPanel's edits would
		// never reach them. See AtomStyleTable's class comment.
		AtomStyleTable original;
		AtomStyleTable copyTakenBeforeEdit = original;

		std::unordered_map<std::string, AtomRenderStyle> styles;
		styles.emplace("Fe", AtomRenderStyle{glm::vec3(0.6f, 0.3f, 0.1f), 0.5f});
		original.ReplaceStyles(std::move(styles), VacancyRenderStyle{});

		ASSERT_EQ(copyTakenBeforeEdit.Size(), 1u);
		EXPECT_NEAR(copyTakenBeforeEdit.GetStyle("Fe").displayRadius, 0.5f, 1e-4f);
	}

	TEST(AtomStyleTableTests, UsesFallbackForUnknownElement)
	{
		AtomStyleTable table;
		const AtomRenderStyle &unknown = table.GetStyle("Xx");
		EXPECT_NEAR(unknown.color.x, 0.70f, 1e-3f);
		EXPECT_NEAR(unknown.color.y, 0.70f, 1e-3f);
		EXPECT_NEAR(unknown.color.z, 0.70f, 1e-3f);
		EXPECT_NEAR(unknown.displayRadius, 0.40f, 1e-3f);

		table.Clear();
		EXPECT_EQ(table.Size(), 0u);
	}
} // namespace DefectStudio::Tests
