#include <gtest/gtest.h>

#include "Core/CoreLayer.hpp"
#include "Debug/DebugLayer.hpp"
#include "Domain/DomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/ImGuiLayer.hpp"
#include "ScientificRuntime/ScientificRuntimeLayer.hpp"
#include "Storage/StorageLayer.hpp"

TEST(AppLayerTests, LayerNamesMatchContracts)
{
	EXPECT_EQ(DefectStudio::CoreLayer().GetName(), "CoreLayer");
	EXPECT_EQ(DefectStudio::DebugLayer().GetName(), "DebugLayer");
	EXPECT_EQ(DefectStudio::DomainLayer().GetName(), "DomainLayer");
	EXPECT_EQ(DefectStudio::IOLayer().GetName(), "IOLayer");
	EXPECT_EQ(DefectStudio::ScientificRuntimeLayer().GetName(), "ScientificRuntimeLayer");
	EXPECT_EQ(DefectStudio::StorageLayer().GetName(), "StorageLayer");
	EXPECT_EQ(DefectStudio::EditorLayer().GetName(), "EditorLayer");
	EXPECT_EQ(DefectStudio::ImGuiLayer(DefectStudio::ImGuiLayerRuntime{}).GetName(), "ImGuiLayer");
}
