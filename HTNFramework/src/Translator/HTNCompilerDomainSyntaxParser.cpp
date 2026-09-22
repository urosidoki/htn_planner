// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNCompilerDomainSyntaxParser.h"

#include "Core/HTNDomainSyntax.h"
#include "Core/HtnSymbol.h"
#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Translator/HTNCompilerDomainLexer.h"
#include "Translator/HTNCompilerDomainLexerContext.h"
#include "Parser/HTNToken.h"
#include "Parser/HTNTokenType.h"

#include <utility>
#include <vector>

namespace
{
namespace AST = HTNCompilerAST;
using Type = HTNTokenType;
thread_local HTNSourceRange ErrorRange;
thread_local HTNParserError Error;

struct Form
{
    Type TokenType = Type::END_OF_FILE;
    HTNAtomOwner Atom;
    HTNSourceRange Range;
    bool IsList = false;
    std::vector<Form> Items;
};

void Invalid(HTNParserErrorCode inCode, const std::string& inMessage)
{
    if (!Error.HasError()) Error = {inCode, inMessage, ErrorRange};
}

Form ReadForm(const std::vector<HTNToken>& inTokens, size_t& ioPosition)
{
    if (ioPosition >= inTokens.size())
    {
        Invalid(HTNParserErrorCode::TokenOutOfBounds, "Unexpected end of compiler syntax");
        return {};
    }
    const HTNToken& Token = inTokens[ioPosition++];
    ErrorRange = Token.GetSourceRange();
    Form Result;
    Result.TokenType = Token.GetType();
    Result.Atom = Token.GetValue();
    Result.Range = Token.GetSourceRange();
    if (Result.TokenType == Type::RIGHT_PARENTHESIS || Result.TokenType == Type::END_OF_FILE)
    {
        Invalid(HTNParserErrorCode::UnexpectedToken, "Unexpected compiler syntax token");
        return {};
    }
    if (Result.TokenType != Type::LEFT_PARENTHESIS) return Result;

    Result.IsList = true;
    while (ioPosition < inTokens.size() && inTokens[ioPosition].GetType() != Type::RIGHT_PARENTHESIS)
    {
        if (inTokens[ioPosition].GetType() == Type::END_OF_FILE)
        {
            Invalid(HTNParserErrorCode::UnclosedList, "Unclosed compiler syntax list");
            return {};
        }
        Result.Items.push_back(ReadForm(inTokens, ioPosition));
        if (Error.HasError()) return {};
    }
    if (ioPosition >= inTokens.size())
    {
        Invalid(HTNParserErrorCode::UnclosedList, "Unclosed compiler syntax list");
        return {};
    }
    Result.Range.End = inTokens[ioPosition++].GetSourceRange().End;
    return Result;
}

const Form& At(const std::vector<Form>& inItems, size_t inIndex)
{
    if (inIndex >= inItems.size())
    {
        Invalid(HTNParserErrorCode::IncompleteSyntax, "Incomplete compiler syntax");
        static const Form Empty;
        return Empty;
    }
    return inItems[inIndex];
}

bool Is(const Form& inForm, Type inType)
{
    return !inForm.IsList && inForm.TokenType == inType;
}

std::string Name(const Form& inForm)
{
    if (!Is(inForm, Type::IDENTIFIER))
    {
        Invalid(HTNParserErrorCode::ExpectedIdentifier, "Expected compiler identifier");
        return {};
    }
    return HTNAtomGetValue<std::string>(*inForm.Atom.Get());
}

template <typename T>
void Source(T& outNode, const HTNSourceRange& inRange, uint32_t inFileIndex)
{
    outNode.Range = inRange;
    outNode.FileIndex = inFileIndex;
}

AST::ValuePtr Identifier(const Form& inForm, uint32_t inFileIndex, const std::string& inName = {})
{
    auto Result = std::make_shared<AST::Value>();
    Result->Kind = AST::ValueKind::Identifier;
    Result->Atom = HTNAtomOwner(inName.empty() ? Name(inForm) : inName);
    Source(*Result, inForm.Range, inFileIndex);
    return Result;
}

HTNAtomOwner Literal(const Form& inForm)
{
    if (inForm.IsList)
    {
        if (inForm.Items.empty())
        {
            Invalid(HTNParserErrorCode::EmptyLiteralList, "Empty literal list");
            return HTNAtomOwner("");
        }
        HTNAtomOwner Result;
        for (const Form& Child : inForm.Items)
        {
            const HTNAtomOwner Element = Literal(Child);
            if (Error.HasError()) return HTNAtomOwner("");
            Result.PushBackElementToList(*Element.Get());
        }
        return Result;
    }
    if (Is(inForm, Type::IDENTIFIER))
        return HTNAtomOwner(HtnSymbol::sGetSymbol(Name(inForm)));
    if (!Is(inForm, Type::TRUE) && !Is(inForm, Type::FALSE) &&
        !Is(inForm, Type::NUMBER) && !Is(inForm, Type::STRING))
    {
        Invalid(HTNParserErrorCode::ExpectedLiteral, "Expected compiler literal");
        return HTNAtomOwner("");
    }
    return inForm.Atom;
}

AST::ValuePtr Argument(const std::vector<Form>& inItems, size_t& ioIndex, uint32_t inFileIndex)
{
    const Form& Head = At(inItems, ioIndex++);
    auto Result = std::make_shared<AST::Value>();
    Source(*Result, Head.Range, inFileIndex);
    if (Is(Head, Type::QUESTION_MARK) || Is(Head, Type::AT))
    {
        const Form& Id = At(inItems, ioIndex++);
        if (Error.HasError()) return {};
        Result->Kind = Is(Head, Type::QUESTION_MARK) ? AST::ValueKind::Variable : AST::ValueKind::Constant;
        Result->Atom = HTNAtomOwner(Name(Id));
        Result->Range.End = Id.Range.End;
    }
    else if (Head.IsList && !Head.Items.empty() &&
             (Is(Head.Items.front(), Type::PLUS) || Is(Head.Items.front(), Type::MINUS) ||
              Is(Head.Items.front(), Type::INCREMENT) || Is(Head.Items.front(), Type::DECREMENT) ||
              Is(Head.Items.front(), Type::MULTIPLY) || Is(Head.Items.front(), Type::DIVIDE) ||
              Is(Head.Items.front(), Type::MODULO)))
    {
        Result->Kind = AST::ValueKind::Arithmetic;
        Result->ArithmeticOp = Is(Head.Items.front(), Type::PLUS) ? AST::ArithmeticOperator::Add :
            Is(Head.Items.front(), Type::MINUS) ? AST::ArithmeticOperator::Subtract :
            Is(Head.Items.front(), Type::INCREMENT) ? AST::ArithmeticOperator::Increment :
            Is(Head.Items.front(), Type::DECREMENT) ? AST::ArithmeticOperator::Decrement :
            Is(Head.Items.front(), Type::MULTIPLY) ? AST::ArithmeticOperator::Multiply :
            Is(Head.Items.front(), Type::DIVIDE) ? AST::ArithmeticOperator::Divide :
                                                   AST::ArithmeticOperator::Modulo;
        Result->Atom = HTNAtomOwner();
        Result->Range = Head.Range;
        for (size_t I = 1; I < Head.Items.size();)
        {
            Result->ArithmeticOperands.push_back(Argument(Head.Items, I, inFileIndex));
            if (Error.HasError()) return {};
        }
        const size_t Count = Result->ArithmeticOperands.size();
        const bool ValidArity = (Result->ArithmeticOp == AST::ArithmeticOperator::Increment ||
                                 Result->ArithmeticOp == AST::ArithmeticOperator::Decrement) ? Count == 1u :
            Result->ArithmeticOp == AST::ArithmeticOperator::Subtract ? Count >= 1u :
            Result->ArithmeticOp == AST::ArithmeticOperator::Modulo ? Count == 2u : Count >= 2u;
        if (!ValidArity)
        {
            Invalid(HTNParserErrorCode::InvalidArithmeticArity, "Invalid arithmetic expression arity");
            return {};
        }
    }
    else if (Head.IsList && !Head.Items.empty() && Is(Head.Items.front(), Type::CALL))
    {
        Result->Kind = AST::ValueKind::Call;
        Result->CallId = Identifier(At(Head.Items, 1), inFileIndex);
        if (Error.HasError()) return {};
        Result->Atom = Result->CallId->GetValue();
        for (size_t I = 2; I < Head.Items.size();)
        {
            Result->CallArguments.push_back(Argument(Head.Items, I, inFileIndex));
            if (Error.HasError()) return {};
        }
    }
    else
    {
        Result->Kind = AST::ValueKind::Literal;
        Result->Atom = Literal(Head);
    }
    if (Error.HasError()) return {};
    return Result;
}

std::pair<AST::ValuePtr, size_t> QualifiedIdentifier(const std::vector<Form>& inItems,
                                                       size_t inStart, uint32_t inFileIndex)
{
    const Form& First = At(inItems, inStart);
    std::string Id = Name(First);
    if (Error.HasError()) return {{}, inStart};
    size_t Next = inStart + 1;
    if (Next + 2 < inItems.size() && Is(inItems[Next], Type::COLON) &&
        Is(inItems[Next + 1], Type::COLON))
    {
        Id += "::" + Name(inItems[Next + 2]);
        if (Error.HasError()) return {{}, inStart};
        Next += 3;
    }
    return {Identifier(First, inFileIndex, Id), Next};
}

AST::ConditionPtr Condition(const Form& inForm, uint32_t inFileIndex)
{
    ErrorRange = inForm.Range;
    if (!inForm.IsList || inForm.Items.empty())
    {
        Invalid(HTNParserErrorCode::ExpectedCondition, "Expected compiler condition");
        return {};
    }
    const auto& Items = inForm.Items;
    auto Result = std::make_shared<AST::Condition>();
    Source(*Result, inForm.Range, inFileIndex);

    if (Is(Items.front(), Type::AND) || Is(Items.front(), Type::OR) || Is(Items.front(), Type::ALT))
    {
        Result->Kind = Is(Items.front(), Type::AND) ? AST::ConditionKind::And :
            (Is(Items.front(), Type::OR) ? AST::ConditionKind::Or : AST::ConditionKind::Alt);
        Result->Range.Begin = Items.front().Range.Begin;
        if (Items.size() > 1) Result->Range.End = Items.back().Range.End;
        else Result->Range.End = Items.front().Range.End;
        for (size_t I = 1; I < Items.size(); ++I)
        {
            Result->Children.push_back(Condition(Items[I], inFileIndex));
            if (Error.HasError()) return {};
        }
        return Result;
    }
    if (Is(Items.front(), Type::NOT))
    {
        if (Items.size() != 2u)
        {
            Invalid(HTNParserErrorCode::InvalidNotCondition, "Invalid not condition");
            return {};
        }
        Result->Kind = AST::ConditionKind::Not;
        Result->Children.push_back(Condition(At(Items, 1), inFileIndex));
        Result->Range.End = Items.back().Range.End;
        return Result;
    }

    const Type FirstType = Items.front().TokenType;
    if (FirstType == Type::EQUAL_EQUAL || FirstType == Type::NOT_EQUAL ||
        FirstType == Type::LESS || FirstType == Type::LESS_EQUAL ||
        FirstType == Type::GREATER || FirstType == Type::GREATER_EQUAL)
    {
        Result->Kind = AST::ConditionKind::Comparison;
        Result->Operator = FirstType == Type::EQUAL_EQUAL ? 0u : FirstType == Type::NOT_EQUAL ? 1u :
            FirstType == Type::LESS ? 2u : FirstType == Type::LESS_EQUAL ? 3u :
            FirstType == Type::GREATER ? 4u : 5u;
        Result->Range.Begin = Items.front().Range.Begin;
        Result->Range.End = Items.back().Range.End;
        for (size_t I = 1; I < Items.size();)
        {
            Result->Arguments.push_back(Argument(Items, I, inFileIndex));
            if (Error.HasError()) return {};
        }
        if (Result->Arguments.size() != 2u)
        {
            Invalid(HTNParserErrorCode::InvalidComparisonArity, "Comparison requires two arguments");
            return {};
        }
        return Result;
    }

    size_t Index = 0;
    if (Is(Items[Index], Type::QUESTION_MARK))
    {
        Result->Kind = AST::ConditionKind::Call;
        Result->Output = Argument(Items, Index, inFileIndex);
        if (Error.HasError()) return {};
        const Form& Call = At(Items, Index);
        if (!Call.IsList || Call.Items.empty() || !Is(Call.Items[0], Type::CALL))
        {
            Invalid(HTNParserErrorCode::ExpectedBoundCall, "Expected bound call condition");
            return {};
        }
        Result->Id = Identifier(At(Call.Items, 1), inFileIndex);
        for (size_t I = 2; I < Call.Items.size();)
        {
            Result->Arguments.push_back(Argument(Call.Items, I, inFileIndex));
            if (Error.HasError()) return {};
        }
        if (Index + 1 != Items.size()) Invalid(HTNParserErrorCode::UnexpectedBoundCallSyntax, "Unexpected bound call condition syntax");
    }
    else if (Is(Items[Index], Type::CALL))
    {
        Result->Kind = AST::ConditionKind::Call;
        Result->Id = Identifier(At(Items, 1), inFileIndex);
        for (size_t I = 2; I < Items.size();)
        {
            Result->Arguments.push_back(Argument(Items, I, inFileIndex));
            if (Error.HasError()) return {};
        }
    }
    else
    {
        const bool IsAxiom = Is(Items[Index], Type::HASH);
        if (IsAxiom) ++Index;
        auto [Id, Next] = QualifiedIdentifier(Items, Index, inFileIndex);
        if (Error.HasError()) return {};
        Result->Id = std::move(Id);
        Index = Next;
        if (!IsAxiom && HTNAtomToString(Result->Id->GetValue(), false).find("::") != std::string::npos)
            Invalid(HTNParserErrorCode::QualifiedFact, "Fact condition cannot be qualified");
        for (; Index < Items.size();)
        {
            Result->Arguments.push_back(Argument(Items, Index, inFileIndex));
            if (Error.HasError()) return {};
        }
        const std::string IdName = HTNAtomToString(Result->Id->GetValue(), false);
        if (!IsAxiom && (IdName == "split_list" || IdName == "split_list_front" || IdName == "split_list_back"))
        {
            if (Result->Arguments.size() != 3u) Invalid(HTNParserErrorCode::InvalidSplitArity, "split_list requires three arguments");
            Result->Kind = AST::ConditionKind::Split;
            Result->Operator = IdName == "split_list_back" ? 2u : (IdName == "split_list_front" ? 1u : 0u);
            if (Result->Operator == 2u && Result->Arguments.size() == 3u)
                std::swap(Result->Arguments[1], Result->Arguments[2]);
        }
        else Result->Kind = IsAxiom ? AST::ConditionKind::Axiom : AST::ConditionKind::Fact;
    }
    Result->Range.End = Items.back().Range.End;
    return Result;
}

AST::ConditionPtr Body(const Form& inForm, uint32_t inFileIndex)
{
    ErrorRange = inForm.Range;
    if (!inForm.IsList)
    {
        Invalid(HTNParserErrorCode::ExpectedConditionBody, "Expected compiler condition body");
        return {};
    }
    if (inForm.Items.empty()) return {};
    if (Is(inForm.Items.front(), Type::AND) || Is(inForm.Items.front(), Type::OR) ||
        Is(inForm.Items.front(), Type::ALT))
        return Condition(inForm, inFileIndex);
    Invalid(HTNParserErrorCode::ExpectedConditionBody, "Expected and, or or alt condition body");
    return {};
}

AST::TaskPtr Task(const Form& inForm, uint32_t inFileIndex)
{
    ErrorRange = inForm.Range;
    if (!inForm.IsList || inForm.Items.empty())
    {
        Invalid(HTNParserErrorCode::ExpectedTask, "Expected compiler task");
        return {};
    }
    auto Result = std::make_shared<AST::Task>();
    Source(*Result, inForm.Range, inFileIndex);
    size_t Index = 0;
    if (Is(inForm.Items[Index], Type::EXCLAMATION_MARK))
    {
        Result->Kind = AST::TaskKind::Primitive;
        ++Index;
    }
    else if (Is(inForm.Items[Index], Type::AMPERSAND))
    {
        Result->Kind = AST::TaskKind::Deferred;
        ++Index;
    }
    else if (Is(inForm.Items[Index], Type::HASH))
    {
        Invalid(HTNParserErrorCode::AxiomPrefixInTaskList, HTNMakeAxiomPrefixInTaskDiagnostic());
        return {};
    }
    else Result->Kind = AST::TaskKind::Compound;
    auto [Id, Next] = QualifiedIdentifier(inForm.Items, Index, inFileIndex);
    if (Error.HasError()) return {};
    if (Result->Kind == AST::TaskKind::Primitive &&
        HTNAtomToString(Id->GetValue(), false).find("::") != std::string::npos)
        Invalid(HTNParserErrorCode::QualifiedPrimitiveTask, "Primitive task cannot be qualified");
    Result->Id = std::move(Id);
    for (Index = Next; Index < inForm.Items.size();)
    {
        Result->Arguments.push_back(Argument(inForm.Items, Index, inFileIndex));
        if (Error.HasError()) return {};
    }
    return Result;
}

std::shared_ptr<const AST::Branch> Branch(const Form& inForm, uint32_t inFileIndex)
{
    ErrorRange = inForm.Range;
    if (!inForm.IsList || inForm.Items.size() != 3u || !inForm.Items[1].IsList ||
        !inForm.Items[2].IsList)
    {
        Invalid(HTNParserErrorCode::ExpectedBranch, "Expected compiler branch");
        return {};
    }
    auto Result = std::make_shared<AST::Branch>();
    Source(*Result, inForm.Range, inFileIndex);
    Result->Id = Name(inForm.Items[0]);
    Result->Precondition = Body(inForm.Items[1], inFileIndex);
    if (Error.HasError()) return {};
    for (const Form& Child : inForm.Items[2].Items)
    {
        Result->Tasks.push_back(Task(Child, inFileIndex));
        if (Error.HasError()) return {};
    }
    return Result;
}

bool ParseDeclaration(const Form& inForm, uint32_t inFileIndex, AST::Domain& ioDomain)
{
    ErrorRange = inForm.Range;
    if (!inForm.IsList || inForm.Items.size() < 2 || !Is(inForm.Items[0], Type::COLON))
    {
        Invalid(HTNParserErrorCode::ExpectedDeclaration, "Expected compiler declaration");
        return false;
    }
    const auto& Items = inForm.Items;
    if (Is(Items[1], Type::HTN_CONSTANTS))
    {
        auto Group = std::make_shared<AST::ConstantGroup>();
        Source(*Group, inForm.Range, inFileIndex);
        size_t Index = 2;
        Group->Id = "unnamed";
        if (Index < Items.size() && Is(Items[Index], Type::IDENTIFIER)) Group->Id = Name(Items[Index++]);
        if (Index < Items.size() && Is(Items[Index], Type::HTN_BASE))
        {
            Group->IsBase = true;
            ++Index;
        }
        else if (Index < Items.size() && Is(Items[Index], Type::HTN_OVERRIDES))
        {
            Group->OverridesDomain = Name(At(Items, Index + 1));
            if (Error.HasError()) return false;
            Index += 2;
        }
        for (; Index < Items.size(); ++Index)
        {
            const Form& Entry = Items[Index];
            if (!Entry.IsList || Entry.Items.size() != 2u)
            {
                Invalid(HTNParserErrorCode::ExpectedConstant, "Expected compiler constant");
                return false;
            }
            auto Constant = std::make_shared<AST::Constant>();
            Source(*Constant, Entry.Range, inFileIndex);
            Constant->Id = Name(At(Entry.Items, 0));
            if (Error.HasError()) return false;
            size_t ValueIndex = 1;
            Constant->ValueNode = Argument(Entry.Items, ValueIndex, inFileIndex);
            if (Error.HasError()) return false;
            if (Constant->ValueNode->Kind != AST::ValueKind::Literal || ValueIndex != Entry.Items.size())
            {
                Invalid(HTNParserErrorCode::ExpectedConstantLiteral, "Expected constant literal");
                return false;
            }
            Group->Constants.push_back(std::move(Constant));
        }
        ioDomain.ConstantGroups.push_back(std::move(Group));
    }
    else if (Is(Items[1], Type::HTN_AXIOM) || Is(Items[1], Type::HTN_METHOD))
    {
        const bool IsAxiom = Is(Items[1], Type::HTN_AXIOM);
        const Form& Signature = At(Items, 2);
        const std::string Id = Name(At(Signature.Items, 0));
        if (Error.HasError()) return false;
        std::vector<AST::ValuePtr> Parameters;
        for (size_t I = 1; I < Signature.Items.size();)
        {
            auto Parameter = Argument(Signature.Items, I, inFileIndex);
            if (Error.HasError()) return false;
            if (Parameter->Kind != AST::ValueKind::Variable)
            {
                Invalid(HTNParserErrorCode::ExpectedParameterVariable, "Expected parameter variable");
                return false;
            }
            Parameters.push_back(std::move(Parameter));
        }
        size_t Index = 3;
        bool TopLevel = false;
        bool IsBase = false;
        std::string OverridesDomain;
        if (Index < Items.size() && Is(Items[Index], Type::HTN_TOP_LEVEL_METHOD))
        {
            TopLevel = true;
            ++Index;
        }
        else if (Index < Items.size() && Is(Items[Index], Type::HTN_BASE))
        {
            IsBase = true;
            ++Index;
        }
        else if (Index < Items.size() && Is(Items[Index], Type::HTN_OVERRIDES))
        {
            OverridesDomain = Name(At(Items, Index + 1));
            if (Error.HasError()) return false;
            Index += 2;
        }
        if (IsAxiom)
        {
            if (TopLevel)
            {
                Invalid(HTNParserErrorCode::InvalidAxiomVisibility, "Axiom cannot be a top_level_method");
                return false;
            }
            auto Axiom = std::make_shared<AST::Axiom>();
            Source(*Axiom, inForm.Range, inFileIndex);
            Axiom->Id = Id;
            Axiom->IsBase = IsBase;
            Axiom->OverridesDomain = OverridesDomain;
            Axiom->Parameters = std::move(Parameters);
            Axiom->Body = Body(At(Items, Index), inFileIndex);
            if (Error.HasError()) return false;
            if (Index + 1 != Items.size())
            {
                Invalid(HTNParserErrorCode::UnexpectedAxiomSyntax, "Unexpected axiom syntax");
                return false;
            }
            ioDomain.Axioms.push_back(std::move(Axiom));
        }
        else
        {
            auto Method = std::make_shared<AST::Method>();
            Source(*Method, inForm.Range, inFileIndex);
            Method->Id = Id;
            Method->Parameters = std::move(Parameters);
            Method->TopLevel = TopLevel;
            Method->IsBase = IsBase;
            Method->OverridesDomain = OverridesDomain;
            for (; Index < Items.size(); ++Index)
            {
                Method->Branches.push_back(Branch(Items[Index], inFileIndex));
                if (Error.HasError()) return false;
            }
            ioDomain.Methods.push_back(std::move(Method));
        }
    }
    else
    {
        Invalid(HTNParserErrorCode::UnknownDeclaration, "Unknown compiler declaration");
        return false;
    }
    return true;
}
}

bool HTNParseCompilerDomainSyntax(const std::string& inSource,
                                  uint32_t inFileIndex,
                                  HTNCompilerAST::Domain& outDomain,
                                  std::string& outError,
                                  HTNSourceRange* outErrorRange,
                                  HTNDiagnosticSink* outDiagnostics,
                                  const std::string& inFilePath,
                                  HTNParserError* outParseError)
{
    outDomain = {};
    outError.clear();
    if (outParseError) *outParseError = {};
    ErrorRange = {};
    Error = {};
    if (outErrorRange) *outErrorRange = {};
    const auto Fail = [&]()
    {
        outDomain = {};
        outError = Error.Message;
        if (outParseError) *outParseError = Error;
        if (outErrorRange) *outErrorRange = Error.Range;
        return false;
    };
    std::vector<HTNToken> Tokens;
    HTNCompilerDomainLexerContext Context(inSource, Tokens);
    HTNCompilerDomainLexer Lexer;
    if (!Lexer.Lex(Context))
    {
        Error.Message = Context.GetLastErrorMessage().empty()
            ? "Compiler source lexing failed"
            : Context.GetLastErrorMessage();
        ErrorRange = Context.GetLastErrorRange();
        Error.Code = HTNParserErrorCode::LexingFailed;
        Error.Range = ErrorRange;
        if (outDiagnostics)
            outDiagnostics->Error(inFilePath, Error.Message,
                                  HTNDiagnosticRecovery::Fatal, ErrorRange);
        return Fail();
    }
    size_t Position = 0;
    const Form Root = ReadForm(Tokens, Position);
    if (Error.HasError()) return Fail();
    if (Position >= Tokens.size() || Tokens[Position].GetType() != Type::END_OF_FILE)
    {
        if (Position < Tokens.size()) ErrorRange = Tokens[Position].GetSourceRange();
        Invalid(HTNParserErrorCode::TrailingSource, "Unexpected source after compiler domain");
        return Fail();
    }
    if (!Root.IsList || Root.Items.size() < 3 || !Is(Root.Items[0], Type::COLON) ||
        !Is(Root.Items[1], Type::HTN_DOMAIN))
    {
        Invalid(HTNParserErrorCode::ExpectedDomain, "Expected compiler domain");
        return Fail();
    }
    outDomain.Id = Name(Root.Items[2]);
    if (Error.HasError()) return Fail();
    outDomain.Range = Root.Range;
    outDomain.FileIndex = inFileIndex;
    size_t Index = 3;
    if (Index < Root.Items.size() && Is(Root.Items[Index], Type::HTN_TOP_LEVEL_DOMAIN))
    {
        outDomain.IsTopLevel = true;
        ++Index;
    }
    else if (Index < Root.Items.size() && Is(Root.Items[Index], Type::HTN_BASE))
    {
        outDomain.IsBase = true;
        ++Index;
    }
    bool Valid = true;
    for (; Index < Root.Items.size(); ++Index)
    {
        const Form& Declaration = Root.Items[Index];
        ErrorRange = Declaration.Range;
        if (!Declaration.IsList || Declaration.Items.size() < 2u)
            Invalid(HTNParserErrorCode::ExpectedDeclaration, "Expected compiler declaration");
        else
        {
            ParseDeclaration(Declaration, inFileIndex, outDomain);
        }
        if (Error.HasError())
        {
            if (Valid)
            {
                outError = Error.Message;
                if (outParseError) *outParseError = Error;
                if (outErrorRange) *outErrorRange = Error.Range;
            }
            if (outDiagnostics)
                outDiagnostics->Error(inFilePath, Error.Message,
                                      HTNDiagnosticRecovery::Recoverable, Error.Range);
            Valid = false;
            Error = {};
        }
    }
    if (!Valid) outDomain = {};
    return Valid;
}
