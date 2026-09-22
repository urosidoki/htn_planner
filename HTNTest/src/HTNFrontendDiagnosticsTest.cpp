// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Domain/Source/HTNDomainFileSyntax.h"
#include "Parser/HTNToken.h"
#include "Parser/HTNTokenType.h"
#include "Translator/HTNCompilerDomainLexer.h"
#include "Translator/HTNCompilerDomainLexerContext.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "gtest/gtest.h"

namespace
{
bool LoadFrontend(const std::string& inSource, HTNDiagnosticSink& outDiagnostics, const HTNDomainSourceProvider& inProvider = {})
{
    HTNCompilerDomainLoadResult Result;
    return HTNCompilerDomainLoader().LoadFromSource("Root.domain", inSource, inProvider, Result, outDiagnostics);
}

const std::string ValidDomain = "(:domain Root top_level_domain (:method (run) top_level_method (ready () ((!act)))))";

void ExpectLocation(const HTNDiagnostic& inDiagnostic, const std::string& inFile, const std::string& inSource, size_t inOffset)
{
    const auto Position = HTNSourceText(inSource).GetPosition(inOffset);
    EXPECT_EQ(inDiagnostic.FilePath, inFile);
    EXPECT_EQ(inDiagnostic.Range.Begin.Offset, inOffset);
    EXPECT_EQ(inDiagnostic.Range.Begin.Line, Position.Line);
    EXPECT_EQ(inDiagnostic.Range.Begin.Column, Position.Column);
    EXPECT_LE(inDiagnostic.Range.End.Offset, inSource.size());
}
} // namespace

TEST(HTNFrontendDiagnosticsTest, CommentsTerminateAtPhysicalEof)
{

    for (const std::string Suffix : {"//", "// comment without newline", "// comment\n", "// comment\r\n"})
    {
        SCOPED_TRACE(Suffix);
        const std::string             Source = "(fact)" + Suffix;
        std::vector<HTNToken>         Tokens;
        HTNCompilerDomainLexerContext Context(Source, Tokens);
        ASSERT_TRUE(HTNCompilerDomainLexer().Lex(Context));
        EXPECT_EQ(Context.GetPosition(), Source.size());
        EXPECT_TRUE(Context.GetLastErrorMessage().empty());
        ASSERT_EQ(Tokens.size(), 4u);
        EXPECT_EQ(Tokens[0].GetType(), HTNTokenType::LEFT_PARENTHESIS);
        EXPECT_EQ(Tokens[1].GetType(), HTNTokenType::IDENTIFIER);
        EXPECT_EQ(Tokens[2].GetType(), HTNTokenType::RIGHT_PARENTHESIS);
        EXPECT_EQ(Tokens[3].GetType(), HTNTokenType::END_OF_FILE);
        EXPECT_EQ(Tokens[3].GetSourceRange().Begin.Offset, Source.size());
        EXPECT_EQ(Tokens[3].GetSourceRange().End.Offset, Source.size());
        HTNDiagnosticSink Diagnostics;
        EXPECT_TRUE(LoadFrontend(ValidDomain + Suffix, Diagnostics));
        EXPECT_FALSE(Diagnostics.HasErrors());
    }
}

TEST(HTNFrontendDiagnosticsTest, MalformedSourceFailsWithLocatedDiagnostics)
{

    for (const std::string Source : {std::string(""), std::string("// no domain"), std::string("(:domain Root top_level_domain"),
                                     std::string("(:domain Root top_level_domain (:method (run) top_level_method (broken)))"),
                                     std::string("(:domain Root top_level_domain (:unknown))"), ValidDomain + "\n(extra)"})
    {
        SCOPED_TRACE(Source);
        HTNDiagnosticSink Diagnostics;
        EXPECT_FALSE(LoadFrontend(Source, Diagnostics));
        ASSERT_NE(Diagnostics.GetFirstError(), nullptr);
        EXPECT_EQ(Diagnostics.GetFirstError()->FilePath, "Root.domain");
        EXPECT_FALSE(Diagnostics.GetFirstError()->Message.empty());
        EXPECT_GE(Diagnostics.GetFirstError()->Range.Begin.Line, 1);
        EXPECT_LE(Diagnostics.GetFirstError()->Range.Begin.Offset, Source.size());
    }
}

TEST(HTNFrontendDiagnosticsTest, LexicalErrorsPreserveCrLfLocationsWithoutExceptions)
{
    const std::string Prefix = "(:domain Root top_level_domain\r\n  (:method (run) top_level_method (ready () ((!act ";
    struct Case
    {
        std::string Tail;
        std::string Message;
        bool        AtEnd;
    };
    const Case Cases[] = {{"$))))", "not recognized", false},
                          {"\"unclosed", "end of the string", true},
                          {"999999999999999999999999))))", "Number out of bounds", false},
                          {"999999999999999999999999999999999999999999999999.0))))", "Number out of bounds", false}};

    for (const Case& Test : Cases)
    {

        SCOPED_TRACE(Test.Tail);
        const std::string Source = Prefix + Test.Tail;
        HTNDiagnosticSink Diagnostics;
        EXPECT_FALSE(LoadFrontend(Source, Diagnostics));
        ASSERT_NE(Diagnostics.GetFirstError(), nullptr);
        ExpectLocation(*Diagnostics.GetFirstError(), "Root.domain", Source, Test.AtEnd ? Source.size() : Prefix.size());
        EXPECT_NE(Diagnostics.GetFirstError()->Message.find(Test.Message), std::string::npos);
    }
}

TEST(HTNFrontendDiagnosticsTest, IncludeSyntaxReportsSharedCodesAndExactLocations)
{
    struct Case
    {
        const char*        Directive;
        const char*        Marker;
        HTNParserErrorCode Code;
    };
    const Case Cases[] = {{"(:include bad)", "bad", HTNParserErrorCode::ExpectedIncludePath},
                          {"(:include \"unterminated", "\"", HTNParserErrorCode::UnterminatedIncludePath},
                          {"(:include \"\")", "\"", HTNParserErrorCode::ExpectedIncludePath},
                          {"(:include \"Base.domain\" bad)", "bad", HTNParserErrorCode::ExpectedIncludeEnd}};
    for (const Case& Test : Cases)
    {
        const std::string             Source = "// header\r\n  " + std::string(Test.Directive);
        std::vector<HTNDomainInclude> Includes;
        std::string                   Domain;
        HTNParserError                Error;
        EXPECT_FALSE(HTNSplitDomainFile(Source, Includes, Domain, Error));
        EXPECT_EQ(Error.Code, Test.Code);

        {

            SCOPED_TRACE(Source);
            HTNDiagnosticSink Diagnostics;
            EXPECT_FALSE(LoadFrontend(Source, Diagnostics));
            ASSERT_EQ(Diagnostics.GetErrorCount(), 1u);
            ExpectLocation(*Diagnostics.GetFirstError(), "Root.domain", Source, Source.find(Test.Marker));
            EXPECT_EQ(Diagnostics.GetFirstError()->Message, Error.Message);
        }
    }
}

TEST(HTNFrontendDiagnosticsTest, IncludesIgnoreCommentAndStringContents)
{
    const std::string             Source   = "// (:include ignored)\r\n(:include // path follows\r\n \"Base.domain\" // closing follows\r\n)\r\n"
                                             "(:domain Root top_level_domain (:method (run) top_level_method (ready () ((!act \"(:include\")))))"
                                             "// (:include also ignored)";
    const HTNDomainSourceProvider Provider = [](const std::filesystem::path& Path, std::string& Text) {
        if (Path.filename() != "Base.domain")
            return false;
        Text = "(:domain Base base)// no final newline";
        return true;
    };

    {
        HTNDiagnosticSink Diagnostics;
        EXPECT_TRUE(LoadFrontend(Source, Diagnostics, Provider));
        EXPECT_FALSE(Diagnostics.HasErrors());
        const std::string Misplaced = ValidDomain + "\r\n  (:include \"Base.domain\")";
        EXPECT_FALSE(LoadFrontend(Misplaced, Diagnostics, Provider));
        ASSERT_NE(Diagnostics.GetFirstError(), nullptr);
        ExpectLocation(*Diagnostics.GetFirstError(), "Root.domain", Misplaced, Misplaced.find("(:include"));
    }
}

TEST(HTNFrontendDiagnosticsTest, IncludeFailuresPointToTheResponsibleFile)
{
    const std::string Root      = "// header\r\n  (:include \"Base.domain\")\r\n" + ValidDomain;
    const std::string Cycle     = "// child\r\n (:include \"Root.domain\")\r\n(:domain Base base)";
    const std::string Missing   = "// child\r\n (:include \"__htn_missing_diagnostic_fixture__.domain\")\r\n(:domain Base base)";
    const std::string Malformed = "// child\r\n(:domain Base base $)";
    for (const std::string Child : {Cycle, Missing, Malformed})

    {

        SCOPED_TRACE(Child);
        const HTNDomainSourceProvider Provider = [&](const std::filesystem::path& Path, std::string& Text) {
            if (Path.filename() != "Base.domain")
                return false;
            Text = Child;
            return true;
        };
        HTNDiagnosticSink Diagnostics;
        EXPECT_FALSE(LoadFrontend(Root, Diagnostics, Provider));
        ASSERT_EQ(Diagnostics.GetErrorCount(), 1u);
        const size_t Offset = Child == Malformed ? Child.find('$') : Child.find("(:include");
        ExpectLocation(*Diagnostics.GetFirstError(), "Base.domain", Child, Offset);
    }
}
