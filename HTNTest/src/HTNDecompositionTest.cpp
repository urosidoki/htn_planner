// Copyright (c) 2023 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNFileHelpers.h"
#include "Core/HTNTask.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Translator/HTNCCodeGenerator.h"
#include "Translator/HTNCompilerIRBuilder.h"
#include "Translator/HTNCompilerDomainSyntaxParser.h"
#include "Translator/HTNCompilerDomainValidator.h"
#include "Translator/HTNTranslation.h"
#include "HTNCoreMinimal.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"

#include "optick.h"
#include "gtest/gtest-param-test.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <execution>
#include <fstream>
#include <filesystem>
#include <format>
#include <map>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

extern "C" const HTNGeneratedPlannerDefinition* CreateWandererHTN_GetDefinition(void);

namespace
{
const std::string kMainDefaultTopLevelMethodID      = "behave";
const std::string kUpperBodyDefaultTopLevelMethodID = "behave_upper_body";

class CompilerLoaderForTest
{
public:
    bool Load(const std::string& inPath, HTNCompilerDomainLoadResult& outResult, std::string& outError) const
    {
        HTNDiagnosticSink Diagnostics;
        if (mLoader.Load(inPath, outResult, Diagnostics))
            return true;
        const HTNDiagnostic* Diagnostic = Diagnostics.GetFirstError();
        outError = Diagnostic ? Diagnostic->Message : "Compiler domain load failed";
        return false;
    }

    bool LoadFromSource(const std::string& inPath, const std::string& inSource,
                        const HTNDomainSourceProvider& inProvider,
                        HTNCompilerDomainLoadResult& outResult, std::string& outError) const
    {
        HTNDiagnosticSink Diagnostics;
        if (mLoader.LoadFromSource(inPath, inSource, inProvider, outResult, Diagnostics))
            return true;
        const HTNDiagnostic* Diagnostic = Diagnostics.GetFirstError();
        outError = Diagnostic ? Diagnostic->Message : "Compiler domain load failed";
        return false;
    }

    bool LoadFromSource(const std::string& inPath, const std::string& inSource,
                        const HTNDomainSourceProvider& inProvider,
                        HTNCompilerDomainLoadResult& outResult,
                        HTNDiagnosticSink& outDiagnostics) const
    {
        return mLoader.LoadFromSource(inPath, inSource, inProvider, outResult, outDiagnostics);
    }

private:
    HTNCompilerDomainLoader mLoader;
};

const HTNCompilerAST::Domain& LoadCompilerSyntaxForTest(const HTNCompilerDomainLoadResult& inLoaded)
{
    return inLoaded.Domain;
}

const HTNCompilerAST::Method* FindMethod(const HTNCompilerAST::Domain& inDomain, const std::string_view inId)
{
    const auto It = std::find_if(inDomain.Methods.begin(), inDomain.Methods.end(),
        [inId](const auto& Method) { return Method && Method->Id == inId; });
    return It == inDomain.Methods.end() ? nullptr : It->get();
}

const HTNCompilerAST::Axiom* FindAxiom(const HTNCompilerAST::Domain& inDomain, const std::string_view inId)
{
    const auto It = std::find_if(inDomain.Axioms.begin(), inDomain.Axioms.end(),
        [inId](const auto& Axiom) { return Axiom && Axiom->Id == inId; });
    return It == inDomain.Axioms.end() ? nullptr : It->get();
}

HTNAtomOwner MakeDeferredTestCell(const int32 inX, const int32 inY)
{
    HTNAtomOwner Cell;
    Cell.PushBackElementToList(HTNAtomOwner(inX));
    Cell.PushBackElementToList(HTNAtomOwner(inY));
    return Cell;
}

bool ReadDeferredTestCell(const HTNAtom& inAtom, int32& outX, int32& outY)
{
    if (HTNAtom_GetType(&inAtom) != HTN_ATOM_TYPE_LIST || HTNAtom_GetListSize(&inAtom) != 2)
        return false;

    const HTNAtom* X = HTNAtom_GetListElement(&inAtom, 0u);
    const HTNAtom* Y = HTNAtom_GetListElement(&inAtom, 1u);
    if (!X || !Y || !HTNAtomIsType<int32>(*X) || !HTNAtomIsType<int32>(*Y))
        return false;

    outX = HTNAtomGetValue<int32>(*X);
    outY = HTNAtomGetValue<int32>(*Y);
    return true;
}

void BindDeferredTestCallTerms(HTNCallTermRegistry& ioRegistry)
{
    ioRegistry.Bind("inc", [](const HTNCallTermArguments& inArguments) -> int32
    {
        return inArguments.size() == 1u && HTNAtomIsType<int32>(inArguments[0])
            ? HTNAtomGetValue<int32>(inArguments[0]) + 1
            : 0;
    });

    ioRegistry.Bind("same_location", [](const HTNCallTermArguments& inArguments) -> bool
    {
        int32 FromX = 0;
        int32 FromY = 0;
        int32 ToX = 0;
        int32 ToY = 0;
        return inArguments.size() == 2u &&
               ReadDeferredTestCell(inArguments[0], FromX, FromY) &&
               ReadDeferredTestCell(inArguments[1], ToX, ToY) &&
               FromX == ToX && FromY == ToY;
    });

    ioRegistry.Bind("both_coordinates_even", [](const HTNCallTermArguments& inArguments) -> bool
    {
        int32 X = 0;
        int32 Y = 0;
        return inArguments.size() == 1u && ReadDeferredTestCell(inArguments[0], X, Y) &&
               X % 2 == 0 && Y % 2 == 0;
    });

    ioRegistry.Bind("both_coordinates_odd", [](const HTNCallTermArguments& inArguments) -> bool
    {
        int32 X = 0;
        int32 Y = 0;
        return inArguments.size() == 1u && ReadDeferredTestCell(inArguments[0], X, Y) &&
               X % 2 != 0 && Y % 2 != 0;
    });
}

void SetDeferredTestLocation(HTNWorldState& ioWorldState, const int32 inX, const int32 inY)
{
    while (ioWorldState.GetFactArgumentsCollectionSize("wanderer_location", 1u) > 0u)
        ioWorldState.RemoveFact("wanderer_location", 1u, 0u);
    ioWorldState.AddFact("wanderer_location", std::vector<HTNAtomOwner>{MakeDeferredTestCell(inX, inY)});
}

void AddDeferredTestPath(HTNWorldState& ioWorldState)
{
    const HTNAtomOwner From = MakeDeferredTestCell(0, 0);
    const HTNAtomOwner To = MakeDeferredTestCell(15, 0);

    ioWorldState.AddFact("pathfind_state", std::vector<HTNAtomOwner>{
        HTNAtomOwner(HtnSymbol::sGetSymbol("succeeded")), From, To, HTNAtomOwner(1)});
    ioWorldState.AddFact("pathfinder", std::vector<HTNAtomOwner>{
        HTNAtomOwner(1), HTNAtomOwner(1), From, To});
    ioWorldState.AddFact("pathfinding_segment", std::vector<HTNAtomOwner>{
        HTNAtomOwner(1), HTNAtomOwner(0), To});
}

void RunDeferredWorldStateTest(const HTNGeneratedPlannerDefinition* inGeneratedDefinition)
{
    ASSERT_NE(inGeneratedDefinition, nullptr);

    HTNDatabaseHook Database;
    HTNCallTermRegistry CallTermRegistry;
    BindDeferredTestCallTerms(CallTermRegistry);
    const auto Run = [&](auto& PlannerHook, auto& PlanningUnit)
    {
        PlannerHook.GetWorldState().AddFact("wanderer_state", std::vector<HTNAtomOwner>{
            HTNAtomOwner(HtnSymbol::sGetSymbol("walking"))});
        SetDeferredTestLocation(PlannerHook.GetWorldState(), 0, 0);
        PlannerHook.GetWorldState().AddFact("wanderer_destination", std::vector<HTNAtomOwner>{
            MakeDeferredTestCell(15, 0)});
        AddDeferredTestPath(PlannerHook.GetWorldState());

        EXPECT_EQ(
            PlanningUnit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("talk_about_destination")),
            HTN_DECOMPOSITION_INVALID_CALL);
        EXPECT_TRUE(PlanningUnit.GetCurrentPlan().empty());

        ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
        ASSERT_EQ(PlanningUnit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
        ASSERT_NE(PlanningUnit.GetCurrentPrimitiveTask(), nullptr);
        EXPECT_EQ(HTNGetTaskHead(*PlanningUnit.GetCurrentPrimitiveTask())->GetString(), "!walk_segment");

        // A deferred target must observe the real state at the moment it is reached.
        // With no current location, resolution fails and owns the cleanup itself.
        PlanningUnit.CompleteCurrentPrimitiveTask();
        PlannerHook.GetWorldState().RemoveFact("wanderer_location", 1u, 0u);
        EXPECT_EQ(PlanningUnit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::Failed);
        EXPECT_TRUE(PlanningUnit.GetCurrentPlan().empty());
        EXPECT_EQ(PlanningUnit.GetCurrentPrimitiveTaskIndex(), 0u);

        // The same unit and generated execution storage remain reusable after failure.
        SetDeferredTestLocation(PlannerHook.GetWorldState(), 0, 0);
        ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
        ASSERT_EQ(PlanningUnit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
        PlanningUnit.CompleteCurrentPrimitiveTask();

        SetDeferredTestLocation(PlannerHook.GetWorldState(), 2, 2);
        ASSERT_EQ(PlanningUnit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
        const HTNAtomOwner* Say = PlanningUnit.GetCurrentPrimitiveTask();
        ASSERT_NE(Say, nullptr);
        ASSERT_NE(HTNGetTaskHead(*Say), nullptr);
        EXPECT_EQ(HTNGetTaskHead(*Say)->GetString(), "!say");
        ASSERT_EQ(HTNGetTaskArgumentCount(*Say), 1u);
        ASSERT_TRUE(HTNAtomIsType<std::string>(HTNGetTaskArgument(*Say, 0u)));
        EXPECT_EQ(HTNAtomGetValue<std::string>(HTNGetTaskArgument(*Say, 0u)),
                  "Both coordinates are even!");

        PlanningUnit.CompleteCurrentPrimitiveTask();
        ASSERT_EQ(PlanningUnit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
        ASSERT_NE(PlanningUnit.GetCurrentPrimitiveTask(), nullptr);
        EXPECT_EQ(HTNGetTaskHead(*PlanningUnit.GetCurrentPrimitiveTask())->GetString(),
                  "!after_destination_comment");
        PlanningUnit.CompleteCurrentPrimitiveTask();
        EXPECT_EQ(PlanningUnit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::PlanCompleted);
    };

    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
    ASSERT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(inGeneratedDefinition));
    HTNPlanningUnit PlanningUnit(Database, PlannerHook, "run");
    Run(PlannerHook, PlanningUnit);
}
} // namespace

TEST(HTNCallTest, UsesOneCallRepresentationForPlainPrimitiveAndDeferredCalls)
{
    const HTNAtomOwner PlainCall = HTNMakeCall(HtnSymbol::sGetSymbol("move_to"), {HTNAtomOwner(1), HTNAtomOwner(2)});
    const HTNAtomOwner PrimitiveCall = HTNMakeCall(HTNMakePrimitiveTaskHead("move_to"), {HTNAtomOwner(1), HTNAtomOwner(2)});
    const HTNAtomOwner DeferredCall = HTNMakeCall(HTNMakeDeferredCallHead("move_to"), {HTNAtomOwner(1), HTNAtomOwner(2)});

    ASSERT_TRUE(HTNIsValidCall(PlainCall));
    ASSERT_TRUE(HTNIsValidCall(PrimitiveCall));
    ASSERT_TRUE(HTNIsValidCall(DeferredCall));
    EXPECT_EQ(HTNGetCallArgumentCount(PlainCall), 2u);
    EXPECT_EQ(HTNGetCallArgumentCount(PrimitiveCall), 2u);
    EXPECT_EQ(HTNGetCallArgumentCount(DeferredCall), 2u);

    EXPECT_EQ(HTNGetPlanStepKind(PlainCall), HTNPlanStepKind::Invalid);
    EXPECT_EQ(HTNGetPlanStepKind(PrimitiveCall), HTNPlanStepKind::PrimitiveTask);
    EXPECT_EQ(HTNGetPlanStepKind(DeferredCall), HTNPlanStepKind::DeferredCall);
}

TEST(HTNCallTest, DeferredCallHeadAddsHashPrefixWithoutChangingTheUnderlyingMethodSymbol)
{
    const HtnSymbol* Method = HtnSymbol::sGetSymbol("do_jump_link");
    const HtnSymbol* Deferred = HTNMakeDeferredCallHead(Method);

    ASSERT_NE(Deferred, nullptr);
    EXPECT_EQ(Deferred, HtnSymbol::sGetSymbol("#do_jump_link"));
    EXPECT_NE(Deferred, Method);
    EXPECT_TRUE(HTNIsDeferredCallHead(Deferred));
    EXPECT_FALSE(HTNIsDeferredCallHead(Method));
}

TEST(HTNCallTest, DeferredPlanStepCanBeRecoveredAsNormalDecomposableCall)
{
    const HTNAtomOwner Deferred = HTNMakeCall(
        HTNMakeDeferredCallHead("do_jump_link"),
        {HTNAtomOwner(7), HTNAtomOwner(std::string("target"))});

    const HTNAtomOwner Call = HTNMakeCallFromDeferredPlanStep(Deferred);

    ASSERT_TRUE(HTNIsValidCall(Call));
    EXPECT_EQ(HTNGetCallHead(Call), HtnSymbol::sGetSymbol("do_jump_link"));
    EXPECT_EQ(HTNGetCallArgumentCount(Call), 2u);
    ASSERT_NE(HTNFindCallArgument(Call, 0u), nullptr);
    ASSERT_NE(HTNFindCallArgument(Call, 1u), nullptr);
    EXPECT_EQ(*HTNFindCallArgument(Call, 0u), HTNAtomOwner(7));
    EXPECT_EQ(*HTNFindCallArgument(Call, 1u), HTNAtomOwner(std::string("target")));
    EXPECT_EQ(HTNGetPlanStepKind(Call), HTNPlanStepKind::Invalid);
}

TEST(HTNTaskTest, RepresentsPrimitiveTaskAsHeadSymbolFollowedByArguments)
{
    const HtnSymbol* Head = HtnSymbol::sGetSymbol("!walk_segment");
    const std::vector<HTNAtomOwner> Arguments{HTNAtomOwner(7), HTNAtomOwner(std::string("north"))};

    const HTNAtomOwner Task = HTNMakeCall(Head, Arguments);

    ASSERT_TRUE(HTNIsValidTask(Task));
    EXPECT_TRUE(HTNIsPrimitiveTask(Task));
    EXPECT_TRUE(Task.IsType<HTNAtomList>());
    ASSERT_EQ(Task.GetListSize(), 3);
    EXPECT_EQ(HTNGetTaskHead(Task), Head);
    EXPECT_EQ(HTNGetTaskArgumentCount(Task), 2u);
    EXPECT_EQ(HTNGetTaskArgument(Task, 0u), Arguments[0]);
    EXPECT_EQ(HTNGetTaskArgument(Task, 1u), Arguments[1]);
}

TEST(HTNTaskTest, PrimitiveTaskHeadPreservesBangAndDiffersFromPlainSymbol)
{
    const HtnSymbol* PlainHead = HtnSymbol::sGetSymbol("walk_segment");
    const HtnSymbol* PrimitiveHead = HTNMakePrimitiveTaskHead(PlainHead);

    ASSERT_NE(PrimitiveHead, nullptr);
    EXPECT_EQ(PrimitiveHead, HtnSymbol::sGetSymbol("!walk_segment"));
    EXPECT_NE(PrimitiveHead, PlainHead);
    EXPECT_TRUE(HTNIsPrimitiveTaskHead(PrimitiveHead));
    EXPECT_FALSE(HTNIsPrimitiveTaskHead(PlainHead));

    const HTNAtomOwner PrimitiveTask = HTNMakeCall(PrimitiveHead, {});
    const HTNAtomOwner PlainSymbolTask = HTNMakeCall(PlainHead, {});
    EXPECT_TRUE(HTNIsPrimitiveTask(PrimitiveTask));
    EXPECT_FALSE(HTNIsPrimitiveTask(PlainSymbolTask));
}

TEST(HTNTaskTest, RejectsAtomsThatDoNotHaveASymbolHead)
{
    const HTNAtomOwner ScalarTask = HTNAtomOwner(42);
    const HTNAtomOwner EmptyListTask = HTNAtomOwner(HTNAtomListOwner{});
    const HTNAtomOwner StringHeadTask = HTNAtomOwner(HTNAtomListOwner{HTNAtomOwner(std::string("walk_segment"))});

    EXPECT_FALSE(HTNIsValidTask(ScalarTask));
    EXPECT_FALSE(HTNIsValidTask(EmptyListTask));
    EXPECT_FALSE(HTNIsValidTask(StringHeadTask));
    EXPECT_EQ(HTNGetTaskHead(StringHeadTask), nullptr);
}


TEST(HTNCoreArchitectureTest, DoesNotExposePlanOrTaskAliases)
{
    const std::filesystem::path TaskHeader = HTNFileHelpers::MakeAbsolutePath(
        "HTNFramework/src/Core/HTNTask.h");
    std::ifstream Input(TaskHeader, std::ios::binary);
    ASSERT_TRUE(Input.good()) << TaskHeader.string();
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_EQ(Text.find("using HTNPlan"), std::string::npos);
    EXPECT_EQ(Text.find("using HTNTask"), std::string::npos);
}






TEST(HTNDeferredCallTest, GeneratedUsesFreshWorldStateAndRecoversAfterResolutionFailure)
{
    const HTNGeneratedPlannerDefinition* Definition = CreateWandererHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    RunDeferredWorldStateTest(Definition);
}

TEST(HTNDeferredCallTest, GeneratedDispatchIncludesDeferredTargetWithoutMakingItTopLevel)
{
    static const std::string Source = R"(
(:domain DeferredCallGenerated top_level_domain
    (:method (run) top_level_method
        (branch
            ()
            ((#later 7))
        )
    )
    (:method (later ?inp_value)
        (branch
            ()
            ((!capture ?inp_value))
        )
    )
)
)";

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.LoadFromSource("DeferredCallGenerated.domain", Source, {}, Linked, Error)) << Error;
    ASSERT_FALSE(FindMethod(Linked.Domain, "later")->IsTopLevel());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_deferred_call.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.EntryPointName = "CreateDeferredCallHTN";
    Options.SourceFilePath = "DeferredCallGenerated.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.OutputSourcePath = OutputPath.string();

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    const std::string DispatchNeedle = "call_head->value.symbol_value ==";
    size_t DispatchCount = 0u;
    for (size_t Position = Generated.find(DispatchNeedle); Position != std::string::npos;
         Position = Generated.find(DispatchNeedle, Position + DispatchNeedle.size()))
    {
        ++DispatchCount;
    }
    EXPECT_EQ(DispatchCount, 2u) << "Generated dispatch should expose exactly the explicit top-level method and the #call target";
    EXPECT_NE(Generated.find("#later"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}











TEST(HTNCompilerArchitectureTest, RejectsMethodParametersWithoutInputPrefix)
{
    const std::string Source =
        "(:domain InvalidParameter top_level_domain\n"
        "  (:method (run) top_level_method (start () ((child 1))))\n"
        "  (:method (child ?value) (body () ((!act ?value))))\n"
        ")\n";
    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    EXPECT_FALSE(Loader.LoadFromSource("InvalidParameter.domain", Source, {}, Linked, Error));
    EXPECT_NE(Error.find("must use the inp_ prefix"), std::string::npos);
    HTNDiagnosticSink Diagnostics;
    EXPECT_FALSE(Loader.LoadFromSource("InvalidParameter.domain", Source, {}, Linked, Diagnostics));
    ASSERT_NE(Diagnostics.GetFirstError(), nullptr);
    EXPECT_EQ(Diagnostics.GetFirstError()->Range.Begin.Line, 3);

    const std::string AxiomSource =
        "(:domain InvalidAxiom top_level_domain\n"
        "  (:axiom (bad ?value) ())\n"
        "  (:method (run) top_level_method (start () ((!act))))\n"
        ")\n";
    EXPECT_FALSE(Loader.LoadFromSource("InvalidAxiom.domain", AxiomSource, {}, Linked, Error));
    EXPECT_NE(Error.find("must use an inp_, out_ or io_ prefix"), std::string::npos);
}

TEST(HTNCompilerArchitectureTest, CompilerSyntaxOwnsLinkedDomainData)
{
    HTNCompilerAST::Domain CompilerDomain;
    std::vector<std::string> SourceFiles;
    {
        HTNCompilerDomainLoadResult Linked;
        std::string Error;
        CompilerLoaderForTest Loader;
        const std::string DomainPath = HTNFileHelpers::MakeAbsolutePath("Domains/Test/complex_scenario.domain").string();
        ASSERT_TRUE(Loader.Load(DomainPath, Linked, Error)) << Error;
        ASSERT_FALSE(Linked.Domain.Id.empty());
        CompilerDomain = std::move(Linked.Domain);
        SourceFiles = Linked.SourceFiles;
    }

    HTNCompilerIR IR;
    std::string Error;
    ASSERT_TRUE(HTNBuildCompilerIR(CompilerDomain, SourceFiles,
                                   HTNGeneratedRuntimeBacktrackingSupport::Disabled, IR, Error)) << Error;
    EXPECT_EQ(IR.DomainId, "ComplexScenario");
    EXPECT_FALSE(IR.Methods.empty());
    EXPECT_FALSE(IR.Axioms.empty());
}

TEST(HTNCompilerArchitectureTest, CompilerLoaderReturnsOwnedSyntaxForIncludedDomain)
{
    const std::string DomainPath = HTNFileHelpers::MakeAbsolutePath("Domains/AAACombatNPC.domain").string();
    HTNCompilerDomainLoadResult Loaded;
    HTNDiagnosticSink Diagnostics;
    HTNCompilerDomainLoader Loader;
    ASSERT_TRUE(Loader.Load(DomainPath, Loaded, Diagnostics));
    EXPECT_FALSE(Diagnostics.HasErrors());
    EXPECT_EQ(Loaded.Domain.GetID(), "AAACombatNPC");
    ASSERT_GT(Loaded.SourceFiles.size(), 1u);

    HTNCompilerIR IR;
    std::string Error;
    ASSERT_TRUE(HTNBuildCompilerIR(Loaded.Domain, Loaded.SourceFiles,
                                   HTNGeneratedRuntimeBacktrackingSupport::Disabled, IR, Error)) << Error;
    EXPECT_FALSE(IR.Methods.empty());
    EXPECT_TRUE(std::any_of(IR.Methods.begin(), IR.Methods.end(), [](const auto& Method)
    {
        return Method.Source.FileIndex > 0u;
    }));
}

TEST(HTNCompilerArchitectureTest, CompilerLoaderAcceptsInMemorySource)
{
    const std::string Source =
        "(:domain CompilerBuffer top_level_domain\n"
        "  (:method (run) top_level_method (ready () ((!act))))\n"
        ")\n";
    HTNCompilerDomainLoadResult Loaded;
    HTNDiagnosticSink Diagnostics;
    HTNCompilerDomainLoader Loader;
    ASSERT_TRUE(Loader.LoadFromSource("CompilerBuffer.domain", Source, {}, Loaded, Diagnostics));
    EXPECT_FALSE(Diagnostics.HasErrors());
    EXPECT_EQ(Loaded.Domain.GetID(), "CompilerBuffer");
    EXPECT_TRUE(std::any_of(Loaded.Domain.GetMethodNodes().begin(), Loaded.Domain.GetMethodNodes().end(),
                            [](const auto& Method) { return Method->GetID() == "run"; }));
}

TEST(HTNCompilerArchitectureTest, CompilerSyntaxValidatorRejectsInvalidMethodParameter)
{
    const std::string Source =
        "(:domain InvalidCompilerSignature top_level_domain\n"
        "  (:method (run ?out_target) top_level_method (ready () ((!act ?out_target))))\n"
        ")\n";
    HTNCompilerAST::Domain Module;
    std::string Error;
    ASSERT_TRUE(HTNParseCompilerDomainSyntax(Source, 0u, Module, Error)) << Error;
    HTNDiagnosticSink Diagnostics;
    EXPECT_FALSE(HTNValidateCompilerDomainModules({Module}, {"InvalidCompilerSignature.domain"},
                                                   true, Diagnostics));
    ASSERT_NE(Diagnostics.GetFirstError(), nullptr);
    EXPECT_EQ(Diagnostics.GetFirstError()->Range.Begin.Line, 2);
    EXPECT_NE(Diagnostics.GetFirstError()->Message.find("inp_ prefix"), std::string::npos);
}

TEST(HTNCompilerArchitectureTest, CompilerLoaderReportsIndependentSyntaxErrors)
{
    const std::string Source =
        "(:domain BrokenCompiler top_level_domain\n"
        "  (:axiom (broken_axiom) (unknown_condition))\n"
        "  (:method (run) top_level_method (broken_branch))\n"
        ")\n";
    HTNCompilerDomainLoader Loader;
    HTNCompilerDomainLoadResult Result;
    HTNDiagnosticSink Diagnostics;
    EXPECT_FALSE(Loader.LoadFromSource("BrokenCompiler.domain", Source, {}, Result, Diagnostics));
    ASSERT_EQ(Diagnostics.GetErrorCount(), 2u);
    EXPECT_FALSE(Diagnostics.HasFatalErrors());
    EXPECT_EQ(Diagnostics.GetDiagnostics()[0].FilePath, "BrokenCompiler.domain");
    EXPECT_EQ(Diagnostics.GetDiagnostics()[0].Range.Begin.Line, 2);
    EXPECT_EQ(Diagnostics.GetDiagnostics()[1].Range.Begin.Line, 3);
}

TEST(HTNCompilerArchitectureTest, CompilerParserRejectsIncompleteFormsWithoutExceptions)
{
    for (const std::string Source : {
             "(:domain Broken top_level_domain (:method (run) top_level_method (branch (and) ((!act))",
             "(:domain Broken top_level_domain (:method (run) top_level_method (branch (and) ((!act ?)))) )",
             "(:domain Broken top_level_domain (:method (run) top_level_method (branch (and (call)) ((!act)))))" })
    {
        HTNCompilerDomainLoader Loader;
        HTNCompilerDomainLoadResult Result;
        HTNDiagnosticSink Diagnostics;
        EXPECT_FALSE(Loader.LoadFromSource("Incomplete.domain", Source, {}, Result, Diagnostics));
        EXPECT_TRUE(Diagnostics.HasErrors());
    }
}

TEST(HTNCompilerArchitectureTest, GeneratedSourceIsDeterministic)
{
    const std::string Relative = "Domains/Test/complex_scenario.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path FirstPath = std::filesystem::temp_directory_path() / "htn_determinism_first.generated.c";
    const std::filesystem::path SecondPath = std::filesystem::temp_directory_path() / "htn_determinism_second.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.EntryPointName = "CreateDeterminismTestHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;

    HTNCCodeGenerator Generator;
    Options.OutputSourcePath = FirstPath.string();
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;
    Options.OutputSourcePath = SecondPath.string();
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream FirstInput(FirstPath, std::ios::binary);
    std::ifstream SecondInput(SecondPath, std::ios::binary);
    ASSERT_TRUE(FirstInput.good());
    ASSERT_TRUE(SecondInput.good());

    const std::string First((std::istreambuf_iterator<char>(FirstInput)), std::istreambuf_iterator<char>());
    const std::string Second((std::istreambuf_iterator<char>(SecondInput)), std::istreambuf_iterator<char>());
    EXPECT_EQ(First, Second) << "Identical linked domains must generate byte-identical C source";

    std::error_code Ec;
    std::filesystem::remove(FirstPath, Ec);
    Ec.clear();
    std::filesystem::remove(SecondPath, Ec);
}

TEST(HTNCompilerArchitectureTest, DefaultBacktrackingPolicyKeepsOverflowFallback)
{
    const std::string Relative = "Domains/Test/complex_scenario.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_default_backtracking.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = OutputPath.string();
    Options.EntryPointName = "CreateDefaultBacktrackingHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_NE(Text.find("HTNGeneratedBacktracking.h"), std::string::npos);
    EXPECT_NE(Text.find("HTNGeneratedBacktrackingOverflow"), std::string::npos);
    EXPECT_NE(Text.find("HTNGeneratedBacktracking_CreateOverflow"), std::string::npos);
    EXPECT_NE(Text.find("pending[32u]"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNCompilerArchitectureTest, GeneratedMethodChoiceCursorsAreMethodLocal)
{
    const std::string Relative = "Domains/Test/complex_scenario.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_method_local_choice_cursors.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = OutputPath.string();
    Options.EntryPointName = "CreateMethodLocalChoiceCursorHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    std::vector<std::string> CursorPreambles;
    size_t Method = 0u;
    while ((Method = Text.find("static int HTN_COMPLEXSCENARIO_METHOD_", Method)) != std::string::npos)
    {
        const size_t BodyBegin = Text.find('{', Method);
        const size_t DeclarationEnd = Text.find(';', Method);
        ASSERT_NE(BodyBegin, std::string::npos);
        if (DeclarationEnd != std::string::npos && DeclarationEnd < BodyBegin)
        {
            Method = DeclarationEnd + 1u;
            continue;
        }
        const size_t PreambleEnd = Text.find("HTN_GENERATED_EVENT_DEBUG_BEGIN_METHOD", Method);
        ASSERT_NE(PreambleEnd, std::string::npos);
        const std::string Preamble = Text.substr(Method, PreambleEnd - Method);

        std::string Cursors;
        size_t Cursor = 0u;
        while ((Cursor = Preamble.find("_choice_cursor_", Cursor)) != std::string::npos)
        {
            const size_t LineBegin = Preamble.rfind('\n', Cursor);
            const size_t LineEnd = Preamble.find('\n', Cursor);
            ASSERT_NE(LineEnd, std::string::npos);
            Cursors.append(Preamble, LineBegin == std::string::npos ? 0u : LineBegin + 1u,
                           LineEnd - (LineBegin == std::string::npos ? 0u : LineBegin + 1u));
            Cursors.push_back('\n');
            Cursor = LineEnd + 1u;
        }
        CursorPreambles.push_back(std::move(Cursors));
        Method = PreambleEnd;
    }

    ASSERT_GT(CursorPreambles.size(), 1u);
    bool FoundDifferentCursorSets = false;
    for (size_t I = 1u; I < CursorPreambles.size(); ++I)
    {
        if (CursorPreambles[I] != CursorPreambles[0])
        {
            FoundDifferentCursorSets = true;
            break;
        }
    }
    EXPECT_TRUE(FoundDifferentCursorSets)
        << "Generated methods should declare only choice cursors reachable from their own branches";

    // A method-local cursor must also be consumed by that method. The generated
    // declaration and its (void) suppression account for two textual references;
    // at least one additional reference proves that the cursor participates in
    // the method's generated condition control flow rather than merely bloating
    // the stack frame.
    static const std::regex CursorIdentifier(R"((?:fact|axiom)_choice_cursor_[0-9]+)");
    Method = 0u;
    while ((Method = Text.find("static int HTN_COMPLEXSCENARIO_METHOD_", Method)) != std::string::npos)
    {
        const size_t BodyBegin = Text.find('{', Method);
        const size_t DeclarationEnd = Text.find(';', Method);
        ASSERT_NE(BodyBegin, std::string::npos);
        if (DeclarationEnd != std::string::npos && DeclarationEnd < BodyBegin)
        {
            Method = DeclarationEnd + 1u;
            continue;
        }
        const size_t BodyEnd = Text.find("\nstatic int HTN_COMPLEXSCENARIO_METHOD_", BodyBegin);
        const std::string Body = Text.substr(BodyBegin,
            BodyEnd == std::string::npos ? std::string::npos : BodyEnd - BodyBegin);

        std::map<std::string, size_t> CursorReferenceCounts;
        for (std::sregex_iterator It(Body.begin(), Body.end(), CursorIdentifier), End; It != End; ++It)
            ++CursorReferenceCounts[It->str()];

        for (const auto& [CursorName, ReferenceCount] : CursorReferenceCounts)
        {
            EXPECT_GT(ReferenceCount, 2u)
                << CursorName << " is declared in a generated method but never used by its condition control flow";
        }

        if (BodyEnd == std::string::npos)
            break;
        Method = BodyEnd + 1u;
    }

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNCompilerArchitectureTest, RuntimeBacktrackingSupportIsDisabledByDefault)
{
    const std::string Relative = "Domains/Test/hierarchical_backtracking.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_runtime_backtracking_disabled.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = OutputPath.string();
    Options.EntryPointName = "CreateRuntimeBacktrackingDisabledHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_EQ(Text.find("context->backtracking_mode"), std::string::npos);
    EXPECT_EQ(Text.find("HTN_BACKTRACKING_FACTS_AND_AXIOMS"), std::string::npos);
    EXPECT_EQ(Text.find("HTN_BACKTRACKING_BRANCHES"), std::string::npos);
    EXPECT_NE(Text.find("HTN_GENERATED_FEATURE_NONE"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNCompilerArchitectureTest, RuntimeBacktrackingSupportCanBeEnabledExplicitly)
{
    const std::string Relative = "Domains/Test/hierarchical_backtracking.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_runtime_backtracking_enabled.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = OutputPath.string();
    Options.EntryPointName = "CreateRuntimeBacktrackingEnabledHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Enabled;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_NE(Text.find("context->backtracking_mode"), std::string::npos);
    EXPECT_NE(Text.find("HTN_BACKTRACKING_FACTS_AND_AXIOMS"), std::string::npos);
    EXPECT_NE(Text.find("HTN_BACKTRACKING_BRANCHES"), std::string::npos);
    EXPECT_NE(Text.find("HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNCompilerArchitectureTest, TranslationRejectsUnknownBacktrackingPolicy)
{
    HTNTranslationRequest Request;
    Request.DomainPath = HTNFileHelpers::MakeAbsolutePath("Domains/Test/complex_scenario.domain");
    Request.EntryPointName = "CreateInvalidBacktrackingPolicyHTN";
    Request.BacktrackingPolicy = static_cast<HTNGeneratedBacktrackingPolicy>(255);

    HTNTranslationResult Result;
    EXPECT_FALSE(HTNTranslateDomain(Request, Result));
    EXPECT_EQ(Result.Failure, HTNTranslationFailure::InvalidOptions);
    EXPECT_EQ(Result.ErrorMessage, "Unknown backtracking policy.");
}

TEST(HTNCompilerArchitectureTest, FixedBacktrackingPolicyGeneratesNoOverflowDependency)
{
    const std::string Relative = "Domains/Test/complex_scenario.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_fixed_backtracking.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = OutputPath.string();
    Options.EntryPointName = "CreateFixedBacktrackingHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedCapacity;
    Options.BacktrackingCapacity = 7u;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_EQ(Text.find("HTNGeneratedBacktracking.h"), std::string::npos);
    EXPECT_EQ(Text.find("HTNGeneratedBacktrackingOverflow"), std::string::npos);
    EXPECT_EQ(Text.find("HTNGeneratedBacktracking_CreateOverflow"), std::string::npos);
    EXPECT_EQ(Text.find("overflow_pending_count"), std::string::npos);
    EXPECT_NE(Text.find("pending[7u]"), std::string::npos);
    EXPECT_EQ(Text.find("#include <assert.h>"), std::string::npos);
    EXPECT_NE(Text.find("HTN_DECOMPOSITION_BACKTRACKING_CAPACITY_EXCEEDED"), std::string::npos);
    EXPECT_NE(Text.find("HTNDecompositionStatus CreateFixedBacktrackingHTN("), std::string::npos);
    EXPECT_EQ(Text.find("if (raw_storage == NULL)\n        return 1;"), std::string::npos);
    EXPECT_NE(Text.find("if (raw_storage == NULL)\n        return;"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNCompilerArchitectureTest, FixedBacktrackingSnapshotCapacityUsesContinuationRestoreSlots)
{
    const std::string Relative = "Domains/Test/atom_list_demo.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_fixed_snapshot_capacity.generated.c";

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = OutputPath.string();
    Options.EntryPointName = "CreateFixedSnapshotCapacityHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedCapacity;
    Options.BacktrackingCapacity = 7u;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    // This domain has generated variables, but none of its task continuations needs
    // to restore a variable modified by the preceding task. Snapshot capacity must
    // therefore be zero logically (one physical element is retained only because
    // standard C does not support zero-length arrays), rather than 7 * variable count.
    EXPECT_NE(Text.find("HTNAtom variable_values["), std::string::npos);
    EXPECT_NE(Text.find("uint32_t snapshot_slots[1u]"), std::string::npos);
    EXPECT_NE(Text.find("HTNAtom snapshot_values[1u]"), std::string::npos);
    EXPECT_NE(Text.find("HTNAtom_InitRange(storage->snapshot_values, 0u)"), std::string::npos);
    EXPECT_NE(Text.find("HTNAtom_DestroyRange(storage->snapshot_values, 0u)"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNCompilerArchitectureTest, RejectsZeroBacktrackingCapacity)
{
    const std::string Relative = "Domains/Test/complex_scenario.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = (std::filesystem::temp_directory_path() / "htn_invalid_backtracking.generated.c").string();
    Options.EntryPointName = "CreateInvalidBacktrackingHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.BacktrackingCapacity = 0u;

    HTNCCodeGenerator Generator;
    EXPECT_FALSE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error));
    EXPECT_NE(Error.find("Backtracking capacity"), std::string::npos);
}



TEST(HTNDomainIncludeTest, LinksReusableModulesAndGeneratorSeesThem)
{
    const std::string Relative = "Domains/include_demo.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();
    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    EXPECT_NE(FindMethod(Linked.Domain, "run_include_demo"), nullptr);
    EXPECT_NE(FindMethod(Linked.Domain, "react_to_event"), nullptr);
    EXPECT_NE(FindMethod(Linked.Domain, "move_to_objective"), nullptr);
    EXPECT_NE(FindAxiom(Linked.Domain, "has_recent_event"), nullptr);

    size_t RecentEventAxiomCount = 0;
    for (const auto& Axiom : Linked.Domain.Axioms)
        if (Axiom && Axiom->GetID() == "has_recent_event") ++RecentEventAxiomCount;
    EXPECT_EQ(RecentEventAxiomCount, 1u); // perception_common is included through two modules.

    const std::filesystem::path GeneratedPath = std::filesystem::temp_directory_path() / "htn_include_demo.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateIncludeDemoTestHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("react_to_event"), std::string::npos);
    EXPECT_NE(Generated.find("move_to_objective"), std::string::npos);
    EXPECT_NE(Generated.find("has_recent_event"), std::string::npos);
    EXPECT_NE(Generated.find("Domains/Includes/react_to_events.domain"), std::string::npos);
    EXPECT_NE(Generated.find("Domains/Includes/navigation_support.domain"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(GeneratedPath, Ec);
}

TEST(HTNDomainIncludeTest, ComplexScenarioReallyUsesIncludedCombatModule)
{
    const std::string Relative = "Domains/Test/complex_scenario.domain";
    const std::string Absolute = HTNFileHelpers::MakeAbsolutePath(Relative).string();
    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(Absolute, Linked, Error)) << Error;
    ASSERT_FALSE(Linked.Domain.Id.empty());

    EXPECT_NE(FindAxiom(Linked.Domain, "is_threat"), nullptr);
    EXPECT_NE(FindAxiom(Linked.Domain, "can_engage"), nullptr);
    EXPECT_NE(FindMethod(Linked.Domain, "emergency_response"), nullptr);
    EXPECT_NE(FindMethod(Linked.Domain, "combat_response"), nullptr);

    const std::filesystem::path GeneratedPath = std::filesystem::temp_directory_path() / "htn_complex_include_test.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateComplexIncludeTestHTN";
    Options.SourceFilePath = Relative;
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("is_threat"), std::string::npos);
    EXPECT_NE(Generated.find("can_engage"), std::string::npos);
    EXPECT_NE(Generated.find("emergency_response"), std::string::npos);
    EXPECT_NE(Generated.find("combat_response"), std::string::npos);
    EXPECT_NE(Generated.find("// (#is_threat ?inp_entity ?enemy)"), std::string::npos);
    EXPECT_NE(Generated.find("// (call binded_function_with_args \"combat branch selected\")"), std::string::npos);
    EXPECT_NE(Generated.find("// (!log \"Combat response\")"), std::string::npos);
    EXPECT_NE(Generated.find("// (combat_response ?inp_entity ?enemy)"), std::string::npos);
    EXPECT_EQ(Generated.find("/* Domains/Test/complex_scenario.domain:"), std::string::npos);
    EXPECT_EQ(Generated.find(" - task "), std::string::npos);
    EXPECT_EQ(Generated.find(" - branch "), std::string::npos);
    EXPECT_EQ(Generated.find(" - condition "), std::string::npos);
    EXPECT_EQ(Generated.find("HTNAtom_Destroy(&storage->snapshot_values[storage->inline_snapshot_count])"), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_Unbind(&storage->snapshot_values[storage->inline_snapshot_count])"), std::string::npos);

    // The linked-source list, not the generated C text, is the authoritative record
    // of which source modules participated in linking. Generated code is free to omit
    // source-path strings entirely.
    const bool HasCombatReactionsSource = std::any_of(
        Linked.SourceFiles.begin(),
        Linked.SourceFiles.end(),
        [](const std::string& SourceFile)
        {
            return std::filesystem::path(SourceFile).filename() == "combat_reactions.domain";
        });
    EXPECT_TRUE(HasCombatReactionsSource);

    const auto CombatMethod = FindMethod(Linked.Domain, "combat_response");
    ASSERT_NE(CombatMethod, nullptr);
    ASSERT_LT(CombatMethod->FileIndex, Linked.SourceFiles.size());
    EXPECT_EQ(std::filesystem::path(Linked.SourceFiles[CombatMethod->FileIndex]).filename(), "combat_reactions.domain");
    EXPECT_NE(Generated.find("_DEBUG_SOURCE_FILES"), std::string::npos);
    EXPECT_NE(Generated.find("_DEBUG_METHOD_SOURCES"), std::string::npos);
    EXPECT_NE(Generated.find("{" + std::to_string(CombatMethod->FileIndex) + "u," +
        std::to_string(CombatMethod->Range.Begin.Line) + "u," +
        std::to_string(CombatMethod->Range.Begin.Column) + "u,"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(GeneratedPath, Ec);
}







TEST(HTNDomainOverrideTest, EliteNinjaGeneratorContainsStaticOverrideTargets)
{
    HTNCompilerDomainLoadResult Linked; std::string Error; CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(HTNFileHelpers::MakeAbsolutePath("Domains/EliteNinja.domain").string(), Linked, Error)) << Error;
    const auto GeneratedPath = std::filesystem::temp_directory_path() / "elite_ninja_override.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateEliteNinjaOverrideTestHTN";
    Options.SourceFilePath = "Domains/EliteNinja.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;
    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("EnemyBase::do_combat"), std::string::npos);
    EXPECT_NE(Generated.find("Ninja::do_combat"), std::string::npos);
    EXPECT_NE(Generated.find("EliteNinja::do_combat"), std::string::npos);
    std::error_code Ec; std::filesystem::remove(GeneratedPath, Ec);
}


TEST(HTNAxiomOverrideTest, QualifiedCallCanReachBaseImplementation)
{
    const auto Dir = std::filesystem::temp_directory_path() / "htn_axiom_override_qualified_call";
    std::filesystem::create_directories(Dir);
    {
        std::ofstream F(Dir / "Base.domain");
        F << "(:domain Base base\n"
             " (:axiom (can_engage ?inp_target) base (and (base_ready ?inp_target)))\n"
             ")\n";
    }
    {
        std::ofstream F(Dir / "Root.domain");
        F << "(:include \"Base.domain\")\n"
             "(:domain Root top_level_domain\n"
             " (:axiom (can_engage ?inp_target) overrides Base\n"
             "   (and (#Base::can_engage ?inp_target) (root_ready ?inp_target)))\n"
             " (:method (run) top_level_method\n"
             "   (b (and (#can_engage enemy)) ((!engage enemy))))\n"
             ")\n";
    }

    HTNCompilerDomainLoadResult Result;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load((Dir / "Root.domain").string(), Result, Error)) << Error;

    const auto BaseAxiom = FindAxiom(Result.Domain, "Base::can_engage");
    const auto EffectiveAxiom = FindAxiom(Result.Domain, "can_engage");
    ASSERT_NE(BaseAxiom, nullptr);
    ASSERT_NE(EffectiveAxiom, nullptr);
    ASSERT_NE(BaseAxiom, EffectiveAxiom);

    ASSERT_NE(EffectiveAxiom->Body, nullptr);
    ASSERT_EQ(EffectiveAxiom->Body->Kind, HTNCompilerAST::ConditionKind::And);
    ASSERT_EQ(EffectiveAxiom->Body->Children.size(), 2u);
    const auto& BaseCall = EffectiveAxiom->Body->Children.front();
    ASSERT_NE(BaseCall, nullptr);
    ASSERT_EQ(BaseCall->Kind, HTNCompilerAST::ConditionKind::Axiom);
    EXPECT_EQ(HTNAtomToString(BaseCall->Id->GetValue(), false), "Base::can_engage");

    const auto GeneratedPath = Dir / "qualified_axiom.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateQualifiedAxiomTestHTN";
    Options.SourceFilePath = (Dir / "Root.domain").string();
    Options.SourceText = Result.LinkedSourceText;
    Options.LinkedSourceFiles = Result.SourceFiles;
    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Result), Options, Error)) << Error;

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("Base::can_engage"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove_all(Dir, Ec);
}

TEST(HTNAxiomOverrideTest, QualifiedBaseAxiomShortCircuitsGeneratedOr)
{
    const auto Dir = std::filesystem::temp_directory_path() / "htn_axiom_override_generated_or_short_circuit";
    std::filesystem::create_directories(Dir);
    {
        std::ofstream F(Dir / "Base.domain");
        F << "(:domain Base base\n"
             " (:axiom (always_true ?inp_value) base ())\n"
             ")\n";
    }
    {
        std::ofstream F(Dir / "Root.domain");
        F << "(:include \"Base.domain\")\n"
             "(:domain Root top_level_domain\n"
             " (:axiom (test ?inp_value)\n"
             "   (or\n"
             "     (#Base::always_true ?inp_value)\n"
             "     (second_guard ?inp_value)))\n"
             " (:method (run) top_level_method\n"
             "   (b (and (#test enemy)) ((!done enemy))))\n"
             ")\n";
    }

    HTNCompilerDomainLoadResult Result;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load((Dir / "Root.domain").string(), Result, Error)) << Error;

    const auto TestAxiom = FindAxiom(Result.Domain, "test");
    ASSERT_NE(TestAxiom, nullptr);
    ASSERT_NE(TestAxiom->Body, nullptr);
    ASSERT_EQ(TestAxiom->Body->Kind, HTNCompilerAST::ConditionKind::Or);
    ASSERT_EQ(TestAxiom->Body->Children.size(), 2u);
    const auto& BaseCall = TestAxiom->Body->Children.front();
    ASSERT_NE(BaseCall, nullptr);
    ASSERT_EQ(BaseCall->Kind, HTNCompilerAST::ConditionKind::Axiom);
    EXPECT_EQ(HTNAtomToString(BaseCall->Id->GetValue(), false), "Base::always_true");

    const auto GeneratedPath = Dir / "qualified_axiom_or.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateQualifiedAxiomOrTestHTN";
    Options.SourceFilePath = (Dir / "Root.domain").string();
    Options.SourceText = Result.LinkedSourceText;
    Options.LinkedSourceFiles = Result.SourceFiles;
    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Result), Options, Error)) << Error;

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    // The first OR alternative is a qualified empty axiom and is therefore always true.
    // Its generated success edge must go through an explicit bridge that jumps out of
    // the OR; it must never fall through to the second_guard alternative.
    const size_t QualifiedCall = Generated.find("// (#Base::always_true ?inp_value)");
    ASSERT_NE(QualifiedCall, std::string::npos);
    // Axiom output validation/propagation now lives in the generated END_AXIOM helper.
    // The failure edge is emitted before the success edge, so locate the invocation
    // that explicitly closes the axiom with succeeded=1 instead of taking the first
    // textual END_AXIOM occurrence (which belongs to the failure path).
    size_t SuccessfulEnd = Generated.find("END_AXIOM_", QualifiedCall);
    while (SuccessfulEnd != std::string::npos)
    {
        const size_t LineEnd = Generated.find('\n', SuccessfulEnd);
        ASSERT_NE(LineEnd, std::string::npos);
        // Keep this independent of any additional generated END_AXIOM scratch arguments.
        // The success value is the second argument; newer generated helpers append
        // caller-owned scope storage after it.
        const size_t SuccessArgument = Generated.find("(context, 1", SuccessfulEnd);
        if (SuccessArgument != std::string::npos && SuccessArgument < LineEnd)
            break;
        SuccessfulEnd = Generated.find("END_AXIOM_", LineEnd);
    }
    ASSERT_NE(SuccessfulEnd, std::string::npos);

    // END_AXIOM(success) is guarded because output validation can still reject the
    // axiom. Skip that failure branch and follow the actual success goto.
    const size_t ValidationFailureGoto = Generated.find("goto __label", SuccessfulEnd);
    ASSERT_NE(ValidationFailureGoto, std::string::npos);
    const size_t ValidationFailureBlockEnd = Generated.find("}\n", ValidationFailureGoto);
    ASSERT_NE(ValidationFailureBlockEnd, std::string::npos);
    const size_t SuccessGoto = Generated.find("goto __label", ValidationFailureBlockEnd);
    ASSERT_NE(SuccessGoto, std::string::npos);
    const size_t LabelBegin = SuccessGoto + 5u;
    const size_t LabelEnd = Generated.find(';', LabelBegin);
    ASSERT_NE(LabelEnd, std::string::npos);
    const std::string SuccessLabel = Generated.substr(LabelBegin, LabelEnd - LabelBegin);
    const std::string LabelDefinition = SuccessLabel + ":\n";
    const size_t SuccessDefinition = Generated.find(LabelDefinition, LabelEnd);
    ASSERT_NE(SuccessDefinition, std::string::npos);
    const size_t BridgeGoto = Generated.find("goto __label", SuccessDefinition + LabelDefinition.size());
    ASSERT_NE(BridgeGoto, std::string::npos);
    EXPECT_LT(BridgeGoto, Generated.find("BeginFactRowCursor", SuccessDefinition));

    std::error_code Ec;
    std::filesystem::remove_all(Dir, Ec);
}

TEST(HTNAxiomOverrideTest, EmptyBaseAxiomGeneratesForInheritedDomain)
{
    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(HTNFileHelpers::MakeAbsolutePath("Domains/Grunt.domain").string(), Linked, Error)) << Error;

    const auto EffectiveAxiom = FindAxiom(Linked.Domain, "can_use_special_attack");
    ASSERT_NE(EffectiveAxiom, nullptr);
    EXPECT_TRUE(EffectiveAxiom->IsBase);
    EXPECT_EQ(EffectiveAxiom->Body, nullptr);

    const auto GeneratedPath = std::filesystem::temp_directory_path() / "grunt_empty_base_axiom.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateGruntEmptyBaseAxiomTestHTN";
    Options.SourceFilePath = "Domains/Grunt.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("can_use_special_attack"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(GeneratedPath, Ec);
}











TEST(HTNConstantsOverrideTest, EliteNinjaGeneratorContainsOnlyEffectiveCombatAttackValue)
{
    HTNCompilerDomainLoadResult Linked; std::string Error; CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.Load(HTNFileHelpers::MakeAbsolutePath("Domains/EliteNinja.domain").string(), Linked, Error)) << Error;
    const auto GeneratedPath = std::filesystem::temp_directory_path() / "elite_ninja_constants_override.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateEliteNinjaConstantsOverrideTestHTN";
    Options.SourceFilePath = "Domains/EliteNinja.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;
    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("elite_ninja_attack"), std::string::npos);
    EXPECT_EQ(Generated.find("\"normal_attack\""), std::string::npos);
    EXPECT_EQ(Generated.find("\"ninja_attack\""), std::string::npos);
    std::error_code Ec; std::filesystem::remove(GeneratedPath, Ec);
}


TEST(HTNGeneratedTopLevelCallTest, GeneratorBindsTopLevelArgumentsFromCallAtom)
{
    static const std::string Source = R"(
(:domain GeneratedTopLevelArguments top_level_domain
    (:method (get_priority_for_role ?inp_candidate ?inp_role_id ?inp_role_context) top_level_method
        (accept () ((!return ?inp_candidate ?inp_role_id ?inp_role_context)))
    )
)
)";

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.LoadFromSource("GeneratedTopLevelArguments.domain", Source, {}, Linked, Error)) << Error;

    const auto GeneratedPath = std::filesystem::temp_directory_path() / "generated_top_level_arguments.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = GeneratedPath.string();
    Options.EntryPointName = "CreateGeneratedTopLevelArgumentsHTN";
    Options.SourceFilePath = "GeneratedTopLevelArguments.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Generated.find("call_argument_count != 3u"), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_GetListElement(call, 1u)"), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_GetListElement(call, 2u)"), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_GetListElement(call, 3u)"), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_AssignCopy(&HTN_GENERATED_EXECUTION(context)->variables.values["), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_CreateCallFromPointers"), std::string::npos);
    EXPECT_NE(Generated.find("HTNAtom_PushBackListElementMove(out_result, &plan_step)"), std::string::npos);
    EXPECT_EQ(Generated.find("HTNAtom_PushBackListElement(out_result, &plan_step)"), std::string::npos);
    EXPECT_EQ(Generated.find("emit_primitive"), std::string::npos);
    EXPECT_EQ(Generated.find("context->top_level_method"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(GeneratedPath, Ec);
}













TEST(HTNDiagnosticSinkTest, ExactDuplicateDiagnosticsAreSuppressed)
{
    HTNDiagnosticSink Diagnostics;
    HTNSourceRange Range;
    Range.Begin.Offset = 10;
    Range.Begin.Line = 2;
    Range.Begin.Column = 4;
    Range.End.Offset = 13;
    Range.End.Line = 2;
    Range.End.Column = 7;

    Diagnostics.Error("Test.domain", "same error", HTNDiagnosticRecovery::Recoverable, Range);
    Diagnostics.Error("Test.domain", "same error", HTNDiagnosticRecovery::Recoverable, Range);

    EXPECT_EQ(Diagnostics.GetErrorCount(), 1u);
}




TEST(HTNWorldStateQueryTest, MissingFactReadQueriesReturnEmptyWithoutLoggingOrMutation)
{
    HTNWorldState WorldState;
    const std::uint64_t InitialGeneration = WorldState.GetFactStorageGeneration();

    std::vector<HTNAtomOwner> BoundArguments{HTNAtomOwner(int32{7})};
    std::vector<HTNAtomOwner> UnboundArguments{HTNAtomOwner{}};

    testing::internal::CaptureStdout();

    EXPECT_EQ(WorldState.GetFactArgumentsTablesSize("missing"), 0u);
    EXPECT_FALSE(WorldState.ContainsFactArgumentsTable("missing", 1u));
    EXPECT_EQ(WorldState.GetFactArgumentsCollectionSize("missing", 1u), 0u);
    EXPECT_EQ(WorldState.Query("missing", UnboundArguments), 0u);
    EXPECT_EQ(WorldState.Query("missing", BoundArguments), 0u);
    EXPECT_FALSE(WorldState.QueryIndex("missing", 0u, BoundArguments));
    EXPECT_FALSE(WorldState.CheckIndex("missing", 0u, UnboundArguments));
    EXPECT_FALSE(WorldState.ContainsFactArguments("missing", BoundArguments));
    EXPECT_EQ(WorldState.FindFactArgumentsTable("missing", 1u), nullptr);

    const HTNWorldState& ConstWorldState = WorldState;
    EXPECT_EQ(ConstWorldState.FindFactArgumentsTables(HtnSymbol::sGetSymbol("missing")), nullptr);

    const std::string Output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(Output.empty()) << Output;
    EXPECT_EQ(WorldState.GetFactStorageGeneration(), InitialGeneration);
    EXPECT_TRUE(WorldState.GetFacts().empty());
}

TEST(HTNWorldStateQueryTest, WriteFactAppendsDuplicateRows)
{
    HTNFactRegistry Registry;
    const HtnSymbol* Fact = HtnSymbol::sGetSymbol("snapshot_fact");
    ASSERT_NE(Fact, nullptr);
    Registry.Register(Fact);

    HTNWorldState WorldState;
    WorldState.SetFactRegistry(&Registry);

    EXPECT_TRUE(WorldState.WriteFact(Fact, int32{7}));
    EXPECT_TRUE(WorldState.WriteFact(Fact, int32{7}));
    EXPECT_EQ(WorldState.GetFactArgumentsCollectionSize("snapshot_fact", 1u), 2u);

}

TEST(HTNWorldStateQueryTest, RemoveAllFactsClearsRowsAndRetainsFactTableStorage)
{
    HTNFactRegistry Registry;
    const HtnSymbol* Fact = HtnSymbol::sGetSymbol("snapshot_fact_reset");
    ASSERT_NE(Fact, nullptr);
    Registry.Register(Fact);

    HTNWorldState WorldState;
    WorldState.SetFactRegistry(&Registry);
    ASSERT_TRUE(WorldState.WriteFact(Fact, int32{7}));

    const HTNFactArgumentsTable* TableBeforeReset = WorldState.FindFactArgumentsTable(Fact, 1u);
    ASSERT_NE(TableBeforeReset, nullptr);
    const std::uint64_t GenerationBeforeReset = WorldState.GetFactStorageGeneration();

    WorldState.RemoveAllFacts();

    EXPECT_EQ(WorldState.GetFactArgumentsCollectionSize("snapshot_fact_reset", 1u), 0u);
    EXPECT_EQ(WorldState.FindFactArgumentsTable(Fact, 1u), TableBeforeReset);
    EXPECT_EQ(WorldState.GetFactStorageGeneration(), GenerationBeforeReset);

    EXPECT_TRUE(WorldState.WriteFact(Fact, int32{9}));
    EXPECT_EQ(WorldState.FindFactArgumentsTable(Fact, 1u), TableBeforeReset);
    EXPECT_EQ(WorldState.GetFactArgumentsCollectionSize("snapshot_fact_reset", 1u), 1u);
}

TEST(HTNWorldStateQueryTest, FullyBoundQuerySucceedsOnlyWhenExactRowExists)
{
    HTNWorldState WorldState;
    const std::vector<HTNAtomOwner> ExistingArguments{HTNAtomOwner(int32{7})};
    const std::vector<HTNAtomOwner> MissingArguments{HTNAtomOwner(int32{8})};
    WorldState.AddFact("target", ExistingArguments);

    std::vector<HTNAtomOwner> ExistingQuery = ExistingArguments;
    std::vector<HTNAtomOwner> MissingQuery = MissingArguments;

    EXPECT_EQ(WorldState.Query("target", ExistingArguments), 1u);
    EXPECT_EQ(WorldState.Query("target", MissingArguments), 0u);
    EXPECT_TRUE(WorldState.QueryIndex("target", 0u, ExistingQuery));
    EXPECT_FALSE(WorldState.QueryIndex("target", 0u, MissingQuery));
}

TEST(HTNWorldStateQueryTest, MissingFactRemovalIsAnIdempotentSilentNoOp)
{
    HTNWorldState WorldState;
    const std::uint64_t InitialGeneration = WorldState.GetFactStorageGeneration();

    testing::internal::CaptureStdout();
    WorldState.RemoveFact("missing", 1u, 0u);
    const std::string Output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(Output.empty()) << Output;
    EXPECT_EQ(WorldState.GetFactStorageGeneration(), InitialGeneration);
    EXPECT_TRUE(WorldState.GetFacts().empty());
}



TEST(HTNBuiltinComparisonTest, GeneratedCodeUsesBuiltinComparisonWithoutCalltermDispatch)
{
    static const std::string Source = R"(
(:domain BuiltinComparisonGenerated top_level_domain
    (:method (run ?inp_value) top_level_method
        (branch_main
            (and
                (>= ?inp_value 5))
            ((!capture ?inp_value))
        )
    )
)
)";

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.LoadFromSource("BuiltinComparisonGenerated.domain", Source, {}, Linked, Error)) << Error;

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_builtin_comparison.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.EntryPointName = "CreateBuiltinComparisonHTN";
    Options.SourceFilePath = "BuiltinComparisonGenerated.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.OutputSourcePath = OutputPath.string();

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(Generated.find("HTNGeneratedRuntime_EvaluateBuiltinComparison"), std::string::npos);
    EXPECT_NE(Generated.find("_COMPARE_ATOMS("), std::string::npos);
    EXPECT_NE(Generated.find("HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables"), std::string::npos);
    EXPECT_EQ(Generated.find("context->prepared_storage->values["), std::string::npos);
    EXPECT_NE(Generated.find("_DOMAIN_PREPARED(context)->values["), std::string::npos);
    EXPECT_EQ(Generated.find("EvaluateCall"), std::string::npos);

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}

TEST(HTNAnySingletonTest, CompilerRecognizesOnlyTheAnyPrefix)
{
    const auto MakeSource = [](const std::string& inVariable)
    {
        return "(:domain AnyPrefix top_level_domain\n"
               "  (:method (run) top_level_method\n"
               "    (branch_main () ((!capture ?" + inVariable + ")))\n"
               "  )\n"
               ")\n";
    };

    CompilerLoaderForTest Loader;
    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    EXPECT_FALSE(Loader.LoadFromSource(
        "AnyPrefix.domain", MakeSource("any_threat"), {}, Linked, Error));
    EXPECT_NE(Error.find("Singleton variable"), std::string::npos);
    EXPECT_FALSE(Loader.LoadFromSource(
        "AnyPrefix.domain", MakeSource("company_value"), {}, Linked, Error));
    EXPECT_EQ(Error.find("Singleton variable"), std::string::npos);
    EXPECT_FALSE(Loader.LoadFromSource(
        "AnyPrefix.domain", MakeSource("any"), {}, Linked, Error));
    EXPECT_EQ(Error.find("Singleton variable"), std::string::npos);
}

TEST(HTNAnySingletonTest, DistinctSingletonsCompileWithoutGeneratedBindingStorage)
{
    static const std::string Source = R"(
(:domain AnySingletonGenerated top_level_domain
    (:method (run) top_level_method
        (branch_main
            (and
                (threat ?any_threat_0)
                (visible ?any_threat_1))
            ((!capture))
        )
    )
)
)";

    HTNCompilerDomainLoadResult Linked;
    std::string Error;
    CompilerLoaderForTest Loader;
    ASSERT_TRUE(Loader.LoadFromSource("AnySingletonGenerated.domain", Source, {}, Linked, Error)) << Error;

    const std::filesystem::path OutputPath = std::filesystem::temp_directory_path() / "htn_any_singleton.generated.c";
    HTNCCodeGeneratorOptions Options;
    Options.EntryPointName = "CreateAnySingletonHTN";
    Options.SourceFilePath = "AnySingletonGenerated.domain";
    Options.SourceText = Linked.LinkedSourceText;
    Options.LinkedSourceFiles = Linked.SourceFiles;
    Options.OutputSourcePath = OutputPath.string();

    HTNCCodeGenerator Generator;
    ASSERT_TRUE(Generator.Generate(LoadCompilerSyntaxForTest(Linked), Options, Error)) << Error;

    std::ifstream Input(OutputPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Generated((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_NE(Generated.find("any_threat_0"), std::string::npos);
    EXPECT_NE(Generated.find("any_threat_1"), std::string::npos);
    EXPECT_EQ(Generated.find("HTNGeneratedRuntime_BindFactVariableSlot"), std::string::npos)
        << "?any_* singleton fact arguments must remain pure wildcards in generated code";

    std::error_code Ec;
    std::filesystem::remove(OutputPath, Ec);
}


TEST(HTNCompilerArchitectureTest, AtomListApiHasSingleNullableElementLookupPath)
{
    const std::filesystem::path AtomHeader = HTNFileHelpers::MakeAbsolutePath(
        "HTNFramework/src/Core/HTNAtomC.h");
    std::ifstream Input(AtomHeader, std::ios::binary);
    ASSERT_TRUE(Input.good()) << AtomHeader.string();
    const std::string AtomC((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

    EXPECT_NE(AtomC.find("HTNAtomList_Get("), std::string::npos);
    EXPECT_NE(AtomC.find("HTNAtom_GetListElement("), std::string::npos);
    EXPECT_EQ(AtomC.find("HTNAtomList_Find("), std::string::npos);
    EXPECT_EQ(AtomC.find("HTNAtom_FindListElement("), std::string::npos);
    EXPECT_EQ(AtomC.find("HTNAtom_PushBackListElementWithAllocator("), std::string::npos);
}
