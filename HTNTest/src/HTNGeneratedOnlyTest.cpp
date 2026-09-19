// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "gtest/gtest.h"

extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingPolicyOverflowHTN_GetDefinition(void);

TEST(HTNGeneratedOnlyTest, MissingDefinitionFailsWithInvalidContext)
{
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState());
    HTNPlanningUnit Unit(Database, Hook, "run");
    EXPECT_EQ(Unit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_INVALID_CONTEXT);
    EXPECT_TRUE(Unit.GetCurrentPlan().empty());
}

TEST(HTNGeneratedOnlyTest, PlansWithoutParsingDomainAndReusesUnitAfterFailure)
{
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState());
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateBacktrackingPolicyOverflowHTN_GetDefinition()));
    HTNPlanningUnit Unit(Database, Hook, "run");
    const char* Expected[] = {"one", "two", "three"};
    for (int Iteration = 0; Iteration < 16; ++Iteration)
    {
        ASSERT_EQ(Unit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
        ASSERT_EQ(Unit.GetCurrentPlan().size(), 3u);
        for (std::size_t Index = 0; Index < 3u; ++Index)
        {
            const auto& Step = Unit.GetCurrentPlan()[Index];
            ASSERT_NE(HTNGetTaskHead(Step), nullptr);
            EXPECT_EQ(HTNGetTaskHead(Step)->GetString(), "!step");
            ASSERT_EQ(HTNGetTaskArgumentCount(Step), 1u);
            EXPECT_EQ(HTNAtomGetValue<std::string>(HTNGetTaskArgument(Step, 0u)), Expected[Index]);
        }
        ASSERT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("always_fail")), HTN_DECOMPOSITION_NO_PLAN);
        EXPECT_TRUE(Unit.GetCurrentPlan().empty());
        EXPECT_LE(Unit.GetLastDecomposition().GetResult().GetListSize(), 0);
    }
}
