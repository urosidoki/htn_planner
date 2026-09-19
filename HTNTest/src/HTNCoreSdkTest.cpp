// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNPlanner.h"
#include "Core/HTNFileHelpers.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "gtest/gtest.h"

extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingPolicyHTN_GetDefinition(void);

TEST(HTNCoreSdkTest, ValidatesDefinitionsWithoutHooks)
{
    const auto* Definition = CreateBacktrackingPolicyHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(nullptr), 0);
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(Definition), 1);

    auto Invalid = *Definition;
    Invalid.abi_version = 0u;
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(&Invalid), 0);
    Invalid = *Definition;
    Invalid.prepared_storage_size = 0u;
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(&Invalid), 0);
    Invalid = *Definition;
    Invalid.initialize_execution_storage = nullptr;
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(&Invalid), 0);
    Invalid = *Definition;
    Invalid.decompose_call = nullptr;
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(&Invalid), 0);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    Invalid = *Definition;
    Invalid.get_execution_profiling = nullptr;
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(&Invalid), 0);
#endif
    EXPECT_EQ(HTNGeneratedPlanner_ValidateDefinition(Definition), 1);
}

TEST(HTNCoreSdkTest, CoreSourcesDoNotDependOnOptionalIntegration)
{
    const auto Root = HTNFileHelpers::MakeAbsolutePath("HTNFramework/src");
    ASSERT_TRUE(std::filesystem::exists(Root));
    for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
    {
        const auto Extension = Entry.path().extension();
        if (!Entry.is_regular_file() || (Extension != ".h" && Extension != ".cpp"))
            continue;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
        EXPECT_EQ(Text.find("#include \"Hook/"), std::string::npos) << Entry.path().string();
        EXPECT_EQ(Text.find("#include \"HTNIntegration.h\""), std::string::npos) << Entry.path().string();
        EXPECT_EQ(Text.find("HTNPlannerHook"), std::string::npos) << Entry.path().string();
        EXPECT_EQ(Text.find("HTNPlanningUnit"), std::string::npos) << Entry.path().string();
        EXPECT_EQ(Text.find("AIHtnDaemonBase"), std::string::npos) << Entry.path().string();
    }
}
