#include <gtest/gtest.h>

#include "Presentation/Panels/PanelRegistry.hpp"

namespace
{
	class CloneablePanel final : public DefectStudio::IPanel
	{
	public:
		explicit CloneablePanel(std::string title, bool visibleByDefault = true)
			: IPanel(std::move(title), visibleByDefault)
		{
		}

		[[nodiscard]] DefectStudio::Ref<DefectStudio::IPanel> Clone() const override
		{
			return DefectStudio::CreateRef<CloneablePanel>(*this);
		}

		void Render() override
		{
		}
	};
}

TEST(PanelRegistryTests, CloneCreatesDistinctPanelWithCopiedState)
{
	DefectStudio::PanelRegistry registry;
	const auto originalId = registry.Add<CloneablePanel>("Original", false);

	auto original = registry.Get(originalId);
	ASSERT_FALSE(original.expired());
	auto originalLocked = original.lock();
	ASSERT_NE(originalLocked, nullptr);
	EXPECT_EQ(originalLocked->GetTitle(), "Original");
	EXPECT_FALSE(originalLocked->IsVisible());

	const auto cloneId = registry.Clone(originalId);
	ASSERT_NE(cloneId, 0u);
	EXPECT_NE(cloneId, originalId);

	auto clone = registry.Get(cloneId);
	ASSERT_FALSE(clone.expired());
	auto cloneLocked = clone.lock();
	ASSERT_NE(cloneLocked, nullptr);
	EXPECT_EQ(cloneLocked->GetTitle(), "Original copy");
	EXPECT_FALSE(cloneLocked->IsVisible());
	EXPECT_TRUE(registry.Contains(originalId));
	EXPECT_TRUE(registry.Contains(cloneId));
}
