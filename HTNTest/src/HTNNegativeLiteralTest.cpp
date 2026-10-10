// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNFileHelpers.h"
#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Parser/HTNToken.h"
#include "Translator/HTNCCodeGenerator.h"
#include "Translator/HTNCompilerDomainLexer.h"
#include "Translator/HTNCompilerDomainLexerContext.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Translator/HTNCompilerIRBuilder.h"
#include "Translator/HTNGeneratedDebugger.h"
#include "WorldState/Parser/HTNWorldStateLexer.h"
#include "WorldState/Parser/HTNWorldStateLexerContext.h"
#include "WorldState/Parser/HTNWorldStateParser.h"
#include "WorldState/Parser/HTNWorldStateParserContext.h"
#include "HTNGTest.h"

#include <cmath>
#include <fstream>
#include <limits>

extern "C" const HTNGeneratedPlannerDefinition* CreateNegativeLiteralsHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateNegativeLiteralsNoneHTN_GetDefinition(void);

namespace
{
std::string DomainWithValue(const std::string& Value)
{
    return "(:domain Negative top_level_domain\n"
           "    (:method (run) top_level_method\n"
           "        (branch () ((!value " + Value + ")))))";
}

TEST(HTNNegativeLiteralTest, LexerProducesOneTypedTokenIncludingTheSignAndSourceRange)
{
    for (const std::string Value : {"-1", "-1.0", "-0.5", "-123.456", "-2147483648", "-0.0"})
    {
        SCOPED_TRACE(Value);
        const std::string Source = "\n  " + Value;
        std::vector<HTNToken> Tokens;
        HTNCompilerDomainLexerContext Context(Source, Tokens);
        ASSERT_TRUE(HTNCompilerDomainLexer().Lex(Context)) << Context.GetLastErrorMessage();
        ASSERT_EQ(Tokens.size(), 2u);
        const auto& Token = Tokens[0];
        EXPECT_EQ(Token.GetType(), HTNTokenType::NUMBER);
        EXPECT_EQ(Token.GetValue().type, Value.find('.') == std::string::npos ? HTN_ATOM_TYPE_INT : HTN_ATOM_TYPE_FLOAT);
        EXPECT_EQ(Token.GetSourceRange().Begin.Offset, 3u);
        EXPECT_EQ(Token.GetSourceRange().Begin.Line, 2);
        EXPECT_EQ(Token.GetSourceRange().Begin.Column, 3);
        EXPECT_EQ(Token.GetSourceRange().End.Offset, Source.size());
        EXPECT_EQ(Token.GetSourceRange().End.Column, static_cast<int>(3 + Value.size()));
#ifdef HTN_ENABLE_LOGGING
        EXPECT_EQ(Token.GetLexeme(), Value);
#endif
        if (Value == "-2147483648")
        {
            EXPECT_EQ(Token.GetValue().value.int_value, std::numeric_limits<int32>::min());
        }
        if (Value == "-0.0")
        {
            EXPECT_TRUE(std::signbit(Token.GetValue().value.float_value));
        }
    }
}

TEST(HTNNegativeLiteralTest, SubtractAndDecrementRemainOperatorTokens)
{
    const std::string Source = "(- 5 2) (- 1.0) (-- 1.5) (- ?value 1) (- -2 3)";
    std::vector<HTNToken> Tokens;
    HTNCompilerDomainLexerContext Context(Source, Tokens);
    ASSERT_TRUE(HTNCompilerDomainLexer().Lex(Context));
    int Minus = 0, Decrement = 0, Negative = 0;
    for (const auto& Token : Tokens)
    {
        if (Token.GetType() == HTNTokenType::MINUS) ++Minus;
        if (Token.GetType() == HTNTokenType::DECREMENT) ++Decrement;
        if (Token.GetType() == HTNTokenType::NUMBER && Token.GetValue().type == HTN_ATOM_TYPE_INT &&
            Token.GetValue().value.int_value == -2) ++Negative;
    }
    EXPECT_EQ(Minus, 4);
    EXPECT_EQ(Decrement, 1);
    EXPECT_EQ(Negative, 1);
}

TEST(HTNNegativeLiteralTest, AstIrAndGeneratedCUsePreparedNegativeAtomsWithoutRuntimeNegation)
{
    const std::string Source = DomainWithValue("-1 -1.5 -123.456 -2147483648 (-0.500 -1 (-123.456))");
    HTNCompilerDomainLoadResult Loaded;
    HTNDiagnosticSink Diagnostics;
    ASSERT_TRUE(HTNCompilerDomainLoader().LoadFromSource("negative.domain", Source, {}, Loaded, Diagnostics));
    const auto& Arguments = Loaded.Domain.Methods[0]->Branches[0]->Tasks[0]->Arguments;
    ASSERT_EQ(Arguments.size(), 5u);
    EXPECT_EQ(Arguments[0]->Kind, HTNCompilerAST::ValueKind::Literal);
    EXPECT_EQ(Arguments[0]->GetValue().type, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(Arguments[0]->GetValue().value.int_value, -1);
    EXPECT_EQ(Arguments[1]->GetValue().type, HTN_ATOM_TYPE_FLOAT);
    EXPECT_FLOAT_EQ(Arguments[1]->GetValue().value.float_value, -1.5f);
    EXPECT_EQ(Arguments[0]->Range.Begin.Offset, Source.find("-1 "));
    EXPECT_EQ(Arguments[1]->Range.End.Offset, Source.find("-1.5") + 4);
    EXPECT_EQ(Arguments[4]->GetValue().type, HTN_ATOM_TYPE_LIST);
    EXPECT_EQ(Arguments[2]->LiteralText, "-123.456");
    EXPECT_EQ(Arguments[4]->LiteralText, "(-0.500 -1 (-123.456))");

    HTNCompilerIR IR;
    std::string Error;
    ASSERT_TRUE(HTNBuildCompilerIR(Loaded.Domain, Loaded.SourceFiles,
        HTNGeneratedRuntimeBacktrackingSupport::Disabled, IR, Error)) << Error;
    EXPECT_TRUE(IR.ArithmeticExpressions.empty());
    const auto& Task = IR.Tasks[0];
    const auto& Integer = IR.Values[Task.FirstArgument];
    const auto& Float = IR.Values[Task.FirstArgument + 1];
    EXPECT_EQ(Integer.Kind, HTNIRValueKind::Literal);
    EXPECT_EQ(Integer.AtomType, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(Integer.IntValue, -1);
    EXPECT_EQ(Float.AtomType, HTN_ATOM_TYPE_FLOAT);
    EXPECT_FLOAT_EQ(Float.FloatValue, -1.5f);
    EXPECT_NE(Integer.StaticValueIndex, HTN_IR_NO_INDEX);
    EXPECT_EQ(IR.StaticValues[Integer.StaticValueIndex].IntValue, -1);
    EXPECT_EQ(IR.Strings.Values[Float.DebugText], "-1.5");
    EXPECT_EQ(IR.Strings.Values[IR.Values[Task.FirstArgument + 2].DebugText], "-123.456");
    EXPECT_EQ(IR.Strings.Values[IR.Values[Task.FirstArgument + 4].DebugText], "(-0.500 -1 (-123.456))");
    EXPECT_EQ(Float.Source.Range.Begin.Offset, Source.find("-1.5"));
    EXPECT_EQ(Float.Source.Range.Begin.Line, 3);
    EXPECT_EQ(IR.SourceFiles[Float.Source.FileIndex], "negative.domain");

    for (auto Mode : {HTNGeneratedInstrumentation::Full, HTNGeneratedInstrumentation::None})
    {
        const auto Path = HTNFileHelpers::MakeAbsolutePath("build/logs/negative-literal.generated.c");
        std::filesystem::create_directories(Path.parent_path());
        HTNCCodeGeneratorOptions Options;
        Options.OutputSourcePath = Path.string();
        Options.EntryPointName = "CreateNegativeLiteralProbe";
        Options.SourceFilePath = "negative.domain";
        Options.SourceText = Source;
        Options.LinkedSourceFiles = Loaded.SourceFiles;
        Options.Instrumentation = Mode;
        ASSERT_TRUE(HTNCCodeGenerator().Generate(Loaded.Domain, Options, Error)) << Error;
        std::ifstream Input(Path);
        const std::string Generated{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
        EXPECT_NE(Generated.find("atom->value.int_value = -1;"), std::string::npos);
        EXPECT_NE(Generated.find("atom->value.int_value = INT32_MIN;"), std::string::npos);
        EXPECT_NE(Generated.find("atom->value.float_value = -1.5f;"), std::string::npos);
        EXPECT_EQ(Generated.find("EvaluateArithmetic"), std::string::npos);
    }
}

TEST(HTNNegativeLiteralTest, MalformedSignedValuesHaveActionableLocatedDiagnostics)
{
    for (const char* Value : {"-", "-.", "-abc", "--1", "- 1"})
    {
        SCOPED_TRACE(Value);
        const auto Source = DomainWithValue(Value);
        HTNCompilerDomainLoadResult Loaded;
        HTNDiagnosticSink Diagnostics;
        EXPECT_FALSE(HTNCompilerDomainLoader().LoadFromSource("invalid_negative.domain", Source, {}, Loaded, Diagnostics));
        ASSERT_TRUE(Diagnostics.HasErrors());
        const auto& Error = *Diagnostics.GetFirstError();
        const auto Start = HTNSourceText(Source).GetPosition(Source.find(Value));
        EXPECT_EQ(Error.FilePath, "invalid_negative.domain");
        EXPECT_EQ(Error.Range.Begin.Offset, Start.Offset);
        EXPECT_EQ(Error.Range.Begin.Line, Start.Line);
        EXPECT_EQ(Error.Range.Begin.Column, Start.Column);
        EXPECT_NE(Error.Message.find("negative literal"), std::string::npos) << Error.Message;
    }
}

TEST(HTNNegativeLiteralTest, RejectsSignedIntegerAndFloatOverflow)
{
    for (const char* Value : {"-2147483649", "-9999999999999999999999999999999999999999999999999.0"})
    {
        const auto Source = DomainWithValue(Value);
        HTNCompilerDomainLoadResult Loaded;
        HTNDiagnosticSink Diagnostics;
        EXPECT_FALSE(HTNCompilerDomainLoader().LoadFromSource("overflow.domain", Source, {}, Loaded, Diagnostics));
        ASSERT_TRUE(Diagnostics.HasErrors());
        EXPECT_EQ(Diagnostics.GetFirstError()->Message, "Number out of bounds");
        EXPECT_EQ(Diagnostics.GetFirstError()->Range.Begin.Offset, Source.find(Value));
    }
}

TEST(HTNNegativeLiteralTest, WorldStateFileAcceptsTheSameNumericValues)
{
    const std::string Source = "signed_fact -1 -0.5\n";
    std::vector<HTNToken> Tokens;
    HTNWorldStateLexerContext Lexer(Source, Tokens);
    ASSERT_TRUE(HTNWorldStateLexer().Lex(Lexer));
    HTNWorldState World;
    HTNWorldStateParserContext Parser(Tokens, World);
    EXPECT_TRUE(HTNWorldStateParser().Parse(Parser));
    ASSERT_EQ(Tokens.size(), 4u);
    EXPECT_EQ(Tokens[1].GetValue().value.int_value, -1);
    EXPECT_FLOAT_EQ(Tokens[2].GetValue().value.float_value, -0.5f);
}

class HTNNegativeLiteralRuntimeTest : public testing::TestWithParam<bool>
{
protected:
    HTNDatabaseHook Database;
    HTNCallTermRegistry Registry;
    HTNPlannerHook Hook{Database.GetWorldState(), Registry};
    const HTNGeneratedPlannerDefinition* Definition = nullptr;
    HTNGeneratedPlannerContext Context{};
    HTNAtomOwner Plan;
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger Debugger;
#endif

    void SetUp() override
    {
        Definition = GetParam() ? CreateNegativeLiteralsHTN_GetDefinition() : CreateNegativeLiteralsNoneHTN_GetDefinition();
        Registry.Bind("negative_identity", [](const HTNCallTermArguments& Args) { return HTNAtomOwner(Args[0]); });
        Registry.Bind("negative_predicate", [](const HTNCallTermArguments& Args) {
            return Args[0].type == HTN_ATOM_TYPE_INT && Args[0].value.int_value == -1 &&
                   Args[1].type == HTN_ATOM_TYPE_FLOAT && Args[1].value.float_value == -0.5f;
        });
        ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(Definition));
        Database.GetWorldState().SetFactRegistry(&Hook.GetFactRegistry());
        Context.world_state = &Database.GetWorldState();
        Context.callterm_binding_context = &Hook.GetCallTermBindingContext();
        Context.prepared_storage = Hook.GetGeneratedPreparedStorage();
        Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.execution_storage = ::operator new(Definition->execution_storage_size);
        ASSERT_TRUE(Definition->initialize_execution_storage(Context.execution_storage));
#ifdef HTN_DEBUG_DECOMPOSITION
        Debugger.SetEnabled(true);
        Context.debugger = &Debugger;
#endif
    }
    void TearDown() override
    {
        if (Context.execution_storage)
        {
            Definition->destroy_execution_storage(Context.execution_storage);
            ::operator delete(Context.execution_storage);
        }
    }
    template<typename... Args> HTNDecompositionStatus Run(const char* Method, Args&&... Arguments)
    {
        Plan = HTNAtomOwner();
        const HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol(Method), std::forward<Args>(Arguments)...));
        return Definition->decompose_call(&Context, Call.Get(), 1, Plan.Get());
    }
    const HTNAtom& Value(uint32 Step, uint32 Argument) const
    {
        return HTNAtomGetListElement(HTNAtomGetListElement(*Plan.Get(), Step), Argument + 1);
    }
};

TEST_P(HTNNegativeLiteralRuntimeTest, ExactReproducerAndDebuggerPreserveNegativeLiterals)
{
    ASSERT_EQ(Run("run"), HTN_DECOMPOSITION_SUCCEEDED);
    ASSERT_EQ(HTNAtomGetListSize(*Plan.Get()), 2);
    EXPECT_EQ(Value(0, 0).type, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(Value(0, 0).value.int_value, -1);
    EXPECT_EQ(Value(0, 1).type, HTN_ATOM_TYPE_FLOAT);
    EXPECT_FLOAT_EQ(Value(0, 1).value.float_value, -1.5f);
    EXPECT_FLOAT_EQ(Value(1, 0).value.float_value, -1.0f);
#ifdef HTN_DEBUG_DECOMPOSITION
    bool Found = false;
    for (const auto& Node : Debugger.GetNodes())
        if (Node.DisplayName == "(!values -1 -1.5 marker)")
        {
            Found = true;
            EXPECT_NE(Node.Source.DomainPath.find("negative_literals.domain"), std::string::npos);
            EXPECT_EQ(Node.Source.Line, 14u);
            EXPECT_EQ(Node.Source.Column, 17u);
        }
    EXPECT_EQ(Found, GetParam());
#endif
}

TEST_P(HTNNegativeLiteralRuntimeTest, AllValueContextsPreserveTypesAndBindings)
{
    ASSERT_TRUE(Database.GetWorldState().WriteFact(HtnSymbol::sGetSymbol("signed_fact"), -1, -0.5f));
    ASSERT_EQ(Run("contexts"), HTN_DECOMPOSITION_SUCCEEDED);
    ASSERT_EQ(HTNAtomGetListSize(*Plan.Get()), 3);
    EXPECT_FLOAT_EQ(Value(0, 0).value.float_value, -123.456f);
    EXPECT_FLOAT_EQ(Value(0, 1).value.float_value, -1.5f);
    EXPECT_EQ(Value(0, 2).value.int_value, -2);
    EXPECT_EQ(Value(0, 3).value.int_value, -7);
    EXPECT_FLOAT_EQ(HTNAtomGetListElement(Value(0, 4), 1).value.float_value, -0.5f);
    const auto& DynamicList = Value(1, 0);
    ASSERT_EQ(DynamicList.type, HTN_ATOM_TYPE_LIST);
    EXPECT_FLOAT_EQ(HTNAtomGetListElement(DynamicList, 1).value.float_value, -123.456f);
    EXPECT_FLOAT_EQ(HTNAtomGetListElement(DynamicList, 2).value.float_value, -0.5f);
    EXPECT_EQ(HTNAtomGetListElement(DynamicList, 3).value.int_value, -1);
    EXPECT_FLOAT_EQ(Value(2, 0).value.float_value, -1.0f);
#ifdef HTN_DEBUG_DECOMPOSITION
    bool Found = false;
    for (const auto& Node : Debugger.GetNodes())
        if (Node.DisplayName == "(= ?literal -123.456)")
        {
            Found = true;
            EXPECT_EQ(Node.Source.Line, 26u);
            EXPECT_EQ(Node.Source.Column, 17u);
        }
    EXPECT_EQ(Found, GetParam());
#endif
}

TEST_P(HTNNegativeLiteralRuntimeTest, ExistingArithmeticOperatorsRetainTheirMeaning)
{
    ASSERT_EQ(Run("operators", 7), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Value(0, 0).value.int_value, 3);
    EXPECT_FLOAT_EQ(Value(0, 1).value.float_value, -1.0f);
    EXPECT_FLOAT_EQ(Value(0, 2).value.float_value, 0.5f);
    EXPECT_EQ(Value(0, 3).value.int_value, 6);
    EXPECT_EQ(Value(0, 4).value.int_value, -5);
    EXPECT_EQ(Value(0, 5).value.int_value, 1);
    EXPECT_FLOAT_EQ(Value(0, 6).value.float_value, -2.5f);
}

TEST_P(HTNNegativeLiteralRuntimeTest, MinimumIntegerAndNegativeZeroSurviveGeneratedStorage)
{
    ASSERT_EQ(Run("boundaries"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Value(0, 0).value.int_value, std::numeric_limits<int32>::min());
    EXPECT_EQ(Value(0, 1).type, HTN_ATOM_TYPE_FLOAT);
    EXPECT_TRUE(std::signbit(Value(0, 1).value.float_value));
    EXPECT_EQ(Value(0, 2).type, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(Value(0, 2).value.int_value, 0);
    EXPECT_FLOAT_EQ(Value(0, 3).value.float_value, -123.456f);
    EXPECT_EQ(HTNAtomGetListElement(Value(0, 4), 0).value.int_value, std::numeric_limits<int32>::min());
    EXPECT_EQ(Value(0, 5).value.int_value, std::numeric_limits<int32>::min() + 1);
}

TEST_P(HTNNegativeLiteralRuntimeTest, BacktrackingAndDeferredCaptureKeepSignedValues)
{
    auto& World = Database.GetWorldState();
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("candidate"), -1));
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("candidate"), -2));
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("selected"), -3));
    ASSERT_EQ(Run("backtracking"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Value(0, 0).value.int_value, -2);
    EXPECT_EQ(Value(0, 1).value.int_value, -3);
    ASSERT_EQ(Run("deferred"), HTN_DECOMPOSITION_SUCCEEDED);
    const HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("emit"), Value(0, 0)));
    Plan = HTNAtomOwner();
    ASSERT_EQ(Definition->decompose_call(&Context, Call.Get(), 0, Plan.Get()), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_FLOAT_EQ(Value(0, 0).value.float_value, -1.0f);
}

INSTANTIATE_TEST_SUITE_P(InstrumentationModes, HTNNegativeLiteralRuntimeTest, testing::Values(true, false),
    [](const testing::TestParamInfo<bool>& Info) { return Info.param ? "Full" : "None"; });
}
