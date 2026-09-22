// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNCallTermBindingContext.h"
#include "Core/HTNCallTermRegistry.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Core/HTNFileHelpers.h"
#include "Domain/Source/HTNSourceText.h"
#include <fstream>
#include "Core/HTNTask.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "gtest/gtest.h"

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

extern "C" const HTNGeneratedPlannerDefinition* CreateMissingCalltermsHTN_GetDefinition(void);

namespace
{
struct ClientContext
{
    int Reports = 0;
    HTNMissingCallTermReason Reason{};
    std::string Name;
    std::string Daemon;
    std::string Domain;
    std::string File;
    uint32_t Line = 0u;
    uint32_t Column = 0u;
};

void Report(void* inContext, const HTNMissingCallTermInfo* inInfo)
{
    auto& Client = *static_cast<ClientContext*>(inContext);
    ++Client.Reports;
    Client.Reason = inInfo->Reason;
    Client.Name = inInfo->Name ? inInfo->Name : "";
    Client.Daemon = inInfo->DaemonID ? inInfo->DaemonID : "";
    Client.Domain = inInfo->Source.domain ? inInfo->Source.domain : "";
    Client.File = inInfo->Source.file ? inInfo->Source.file : "";
    Client.Line = inInfo->Source.line;
    Client.Column = inInfo->Source.column;
}
}

namespace
{
int InvokeGenerated(const HTNPlannerExecutionContext& inContext, const HTNGeneratedCallTerm& inCall, HTNAtom* outResult)
{
    HTNGeneratedPlannerContext Context{};
    Context.callterm_binding_context = inContext.CallTermBindingContext;
    Context.client_context = inContext.ClientContext;
    Context.missing_callterm_policy = inContext.MissingCallTermPolicy;
    Context.missing_callterm_callback = inContext.MissingCallTermCallback;
    return HTNCallTermRegistry_InvokeGeneratedCallTermWithSource(&Context, &inCall, nullptr, 0u, outResult, nullptr);
}
}

TEST(HTNMissingCallTermTest, ExecutionOptionsDefaultToUnset)
{
    HTNPlannerExecutionContext Context{};
    HTNGeneratedPlannerContext Generated{};
    EXPECT_EQ(Context.MissingCallTermPolicy, HTNMissingCallTermPolicy::Unset);
    EXPECT_EQ(Generated.missing_callterm_policy, HTNMissingCallTermPolicy::Unset);
    EXPECT_EQ(Context.MissingCallTermCallback, nullptr);
    EXPECT_EQ(Generated.missing_callterm_callback, nullptr);
}

TEST(HTNMissingCallTermTest, BothInvocationApisUseTheSamePolicyAndReasons)
{
    HTNCallTermRegistry Registry;
    ASSERT_TRUE(Registry.BindMember("empty", "agent", {}, {}));
    ASSERT_TRUE(Registry.BindMember("member", "agent", [](void*, const HTNCallTermArguments&) { return HTNAtomOwner(true); }, {}));
    Registry.Bind("ordinary_failure", [](const HTNCallTermArguments&) { return HTNAtomOwner(); });
    HTNCallTermBindingContext Bindings(Registry);
    HTNPlannerExecutionContext Context{};
    Context.CallTermBindingContext = &Bindings;
    ClientContext Client;
    Context.ClientContext = &Client;
    Context.MissingCallTermCallback = Report;
    const char* Names[] = {"absent", "empty", "member"};
    const HTNMissingCallTermReason Reasons[] = {HTNMissingCallTermReason::NotRegistered,
        HTNMissingCallTermReason::MissingBinding, HTNMissingCallTermReason::MissingInstance};
    const std::vector<HTNAtomOwner> Arguments;
    for (const auto Policy : {HTNMissingCallTermPolicy::FailSilently, HTNMissingCallTermPolicy::Report})
    {
        Context.MissingCallTermPolicy = Policy;
        for (size_t I = 0; I < 3u; ++I)
        {
            const int Before = Client.Reports;
            const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Bindings, Names[I]);
            HTNAtomOwner Result;
            testing::internal::CaptureStdout();
            testing::internal::CaptureStderr();
            const bool Bound = Registry.Execute(Names[I], Context, Arguments).IsBound();
            const int Invoked = InvokeGenerated(Context, Call, Result.Get());
            const auto Stderr = testing::internal::GetCapturedStderr();
            const auto Stdout = testing::internal::GetCapturedStdout();
            EXPECT_FALSE(Bound);
            EXPECT_EQ(Invoked, 0);
            EXPECT_TRUE(Stdout.empty());
            EXPECT_TRUE(Stderr.empty());
            EXPECT_EQ(Client.Reports - Before, Policy == HTNMissingCallTermPolicy::Report ? 2 : 0);
            if (Policy == HTNMissingCallTermPolicy::Report)
            {
                EXPECT_EQ(Client.Reason, Reasons[I]);
                EXPECT_EQ(Client.Name, Names[I]);
                EXPECT_EQ(Client.Daemon, I == 0u ? "" : "agent");
            }
        }
    }
    const int Before = Client.Reports;
    EXPECT_FALSE(Registry.Execute("ordinary_failure", Context, Arguments).IsBound());
    EXPECT_EQ(Client.Reports, Before);
}

TEST(HTNMissingCallTermTest, GeneratedCallsReportProvenanceAndPreserveFailureSemantics)
{
    std::ifstream Input(HTNFileHelpers::MakeAbsolutePath("Domains/Test/missing_callterms.domain"), std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    const HTNSourceText Source(Text);
    for (const auto Reason : {HTNMissingCallTermReason::NotRegistered, HTNMissingCallTermReason::MissingBinding,
                             HTNMissingCallTermReason::MissingInstance})
    {
        HTNCallTermRegistry Registry;
        if (Reason == HTNMissingCallTermReason::MissingBinding)
            ASSERT_TRUE(Registry.BindMember("probe", "agent", {}, {}));
        if (Reason == HTNMissingCallTermReason::MissingInstance)
            ASSERT_TRUE(Registry.BindMember("probe", "agent", [](void*, const HTNCallTermArguments&) { return HTNAtomOwner(true); }, {}));
        Registry.Bind("identity", [](const HTNCallTermArguments& Args) { return HTNAtomOwner(Args[0]); });
        HTNDatabaseHook Database;
        HTNPlannerHook Hook(Database.GetWorldState(), Registry);
        ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateMissingCalltermsHTN_GetDefinition()));
        HTNPlanningUnit Unit(Database, Hook, "condition");
        ClientContext Client;
        auto& Context = Unit.GetExecutionContext();
        Context.MissingCallTermCallback = Report;
        Unit.SetClientContext(&Client);
        for (const auto Policy : {HTNMissingCallTermPolicy::FailSilently, HTNMissingCallTermPolicy::Report})
        {
            Context.MissingCallTermPolicy = Policy;
            for (const std::string Entry : {"condition", "binding", "primitive", "compound", "nested", "unused"})
            {
                SCOPED_TRACE(Entry);
                const int Before = Client.Reports;
                const bool Succeeds = Entry == "condition" || Entry == "binding" || Entry == "unused";
                EXPECT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol(Entry)),
                          Succeeds ? HTN_DECOMPOSITION_SUCCEEDED : HTN_DECOMPOSITION_NO_PLAN);
                EXPECT_EQ(Client.Reports - Before, Policy == HTNMissingCallTermPolicy::Report && Entry != "unused" ? 1 : 0);
                if (Succeeds)
                {
                    ASSERT_EQ(Unit.GetCurrentPlan().size(), 1u);
                    ASSERT_NE(HTNGetTaskHead(Unit.GetCurrentPlan().front()), nullptr);
                    EXPECT_EQ(HTNGetTaskHead(Unit.GetCurrentPlan().front())->GetString(), Entry == "unused" ? "!safe" : "!fallback");
                }
                if (Policy == HTNMissingCallTermPolicy::Report && Entry != "unused")
                {
                    EXPECT_EQ(Client.Reason, Reason);
                    EXPECT_EQ(Client.Name, "probe");
                    EXPECT_EQ(Client.Domain, "MissingCallTerms");
                    EXPECT_NE(Client.File.find("missing_callterms.domain"), std::string::npos);
                    const size_t Method = Text.find("(:method (" + Entry + ")");
                    ASSERT_NE(Method, std::string::npos);
                    const size_t Call = Text.find(Entry == "binding" ? "(?value" : "(call probe)", Method);
                    ASSERT_NE(Call, std::string::npos);
                    const auto Position = Source.GetPosition(Call);
                    EXPECT_EQ(Client.Line, static_cast<uint32_t>(Position.Line));
                    EXPECT_EQ(Client.Column, static_cast<uint32_t>(Position.Column));
                }
            }
        }
    }
}

TEST(HTNMissingCallTermTest, SharedRegistryKeepsInstancesAndClientContextsIndependent)
{
    HTNCallTermRegistry Registry;
    ASSERT_TRUE(Registry.BindMember("member", "agent", [](void* Instance, const HTNCallTermArguments&) {
        return HTNAtomOwner(*static_cast<int*>(Instance));
    }, {}));
    HTNCallTermBindingContext FirstBindings(Registry), SecondBindings(Registry);
    HTNPlannerExecutionContext First{}, Second{};
    First.CallTermBindingContext = &FirstBindings;
    Second.CallTermBindingContext = &SecondBindings;
    ClientContext FirstClient, SecondClient;
    First.MissingCallTermPolicy = HTNMissingCallTermPolicy::Report;
    First.MissingCallTermCallback = Report;
    First.ClientContext = &FirstClient;
    Second.MissingCallTermPolicy = HTNMissingCallTermPolicy::Report;
    Second.MissingCallTermCallback = Report;
    Second.ClientContext = &SecondClient;
    int Instance = 7;
    ASSERT_TRUE(FirstBindings.SetDaemon("agent", &Instance));
    const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&FirstBindings, "member");
    HTNAtomOwner Result;
    EXPECT_EQ(InvokeGenerated(First, Call, Result.Get()), 1);
    EXPECT_EQ(InvokeGenerated(Second, Call, Result.Get()), 0);
    EXPECT_EQ(FirstClient.Reports, 0);
    EXPECT_EQ(SecondClient.Reports, 1);
    ASSERT_TRUE(SecondBindings.SetDaemon("agent", &Instance));
    EXPECT_EQ(InvokeGenerated(Second, Call, Result.Get()), 1);
    EXPECT_EQ(SecondClient.Reports, 1);
}

TEST(HTNMissingCallTermTest, UnsetAssertsOnMissingInvocation)
{
    HTNCallTermRegistry Registry;
    ASSERT_TRUE(Registry.BindMember("empty", "agent", {}, {}));
    ASSERT_TRUE(Registry.BindMember("member", "agent", [](void*, const HTNCallTermArguments&) { return HTNAtomOwner(true); }, {}));
    HTNCallTermBindingContext Bindings(Registry);
    HTNPlannerExecutionContext Context{};
    Context.CallTermBindingContext = &Bindings;
    const std::vector<HTNAtomOwner> Arguments;
    for (const char* Name : {"absent", "empty", "member"})
    {
        SCOPED_TRACE(Name);
        const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Bindings, Name);
        HTNAtomOwner Result;
#ifndef NDEBUG
        const auto Invoke = [&](bool Generated) {
#if defined(_WIN32) && defined(_DEBUG)
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
            if (Generated)
                (void)InvokeGenerated(Context, Call, Result.Get());
            else
                (void)Registry.Execute(Name, Context, Arguments);
        };
        EXPECT_DEATH(Invoke(false), "Configure the missing callterm policy explicitly");
        EXPECT_DEATH(Invoke(true), "Configure the missing callterm policy explicitly");
#else
        EXPECT_FALSE(Registry.Execute(Name, Context, Arguments).IsBound());
        EXPECT_EQ(InvokeGenerated(Context, Call, Result.Get()), 0);
#endif
    }
}

TEST(HTNMissingCallTermTest, ReportsUseExecutionContextWithSharedBindings)
{
    HTNCallTermRegistry Registry;
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState(), Registry);
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateMissingCalltermsHTN_GetDefinition()));
    HTNPlanningUnit First(Database, Hook, "condition"), Second(Database, Hook, "condition");
    ClientContext FirstClient, SecondClient;
    First.GetExecutionContext().ClientContext = &FirstClient;
    First.GetExecutionContext().MissingCallTermPolicy = HTNMissingCallTermPolicy::Report;
    First.GetExecutionContext().MissingCallTermCallback = Report;
    Second.GetExecutionContext().ClientContext = &SecondClient;
    Second.GetExecutionContext().MissingCallTermPolicy = HTNMissingCallTermPolicy::FailSilently;
    ASSERT_EQ(First.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    ASSERT_EQ(Second.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FirstClient.Reports, 1);
    EXPECT_EQ(SecondClient.Reports, 0);
    First.GetExecutionContext().ClientContext = &SecondClient;
    ASSERT_EQ(First.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FirstClient.Reports, 1);
    EXPECT_EQ(SecondClient.Reports, 1);
}

TEST(HTNMissingCallTermTest, InvalidRuntimePolicyAndMissingReportCallbackAssert)
{
    HTNCallTermRegistry Registry;
    HTNCallTermBindingContext Bindings(Registry);
    HTNPlannerExecutionContext Context{};
    Context.CallTermBindingContext = &Bindings;
    const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Bindings, "absent");
    const std::vector<HTNAtomOwner> Arguments;
    for (const auto Policy : {HTNMissingCallTermPolicy::Report, static_cast<HTNMissingCallTermPolicy>(99)})
    {
        Context.MissingCallTermPolicy = Policy;
        HTNAtomOwner Result;
#ifndef NDEBUG
        const auto Invoke = [&](bool Generated) {
#if defined(_WIN32) && defined(_DEBUG)
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
            if (Generated) (void)InvokeGenerated(Context, Call, Result.Get());
            else (void)Registry.Execute("absent", Context, Arguments);
        };
        const char* Message = Policy == HTNMissingCallTermPolicy::Report
            ? "Report policy requires" : "Invalid missing callterm policy";
        EXPECT_DEATH(Invoke(false), Message);
        EXPECT_DEATH(Invoke(true), Message);
#else
        EXPECT_FALSE(Registry.Execute("absent", Context, Arguments).IsBound());
        EXPECT_EQ(InvokeGenerated(Context, Call, Result.Get()), 0);
#endif
    }
}
