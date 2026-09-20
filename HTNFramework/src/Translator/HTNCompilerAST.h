// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomOwner.h"
#include "Domain/Source/HTNSourceText.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Owned compiler syntax used by validation, linking and IR lowering.
namespace HTNCompilerAST
{
struct Node
{
    virtual ~Node() = default;
    HTNSourceRange Range;
    uint32_t FileIndex = 0;
    const HTNSourceRange& GetSourceRange() const { return Range; }
};

enum class ValueKind : uint8_t { Identifier, Literal, Variable, Constant, Call, Arithmetic };
enum class ArithmeticOperator : uint8_t { Add, Subtract, Multiply, Divide, Modulo, Increment, Decrement };
struct Value : Node
{
    ValueKind Kind = ValueKind::Literal;
    HTNAtomOwner Atom;
    std::shared_ptr<const Value> CallId;
    std::vector<std::shared_ptr<const Value>> CallArguments;
    ArithmeticOperator ArithmeticOp = ArithmeticOperator::Add;
    std::vector<std::shared_ptr<const Value>> ArithmeticOperands;
    const HTNAtom& GetValue() const { return *Atom.Get(); }
    ValueKind GetExpressionType() const { return Kind; }
    const auto& GetIDNode() const { return CallId; }
    const auto& GetArgumentNodes() const { return CallArguments; }
    ArithmeticOperator GetArithmeticOperator() const { return ArithmeticOp; }
    const auto& GetArithmeticOperandNodes() const { return ArithmeticOperands; }
};
using ValuePtr = std::shared_ptr<const Value>;

enum class ConditionKind : uint8_t { Fact, Axiom, Call, Comparison, Split, And, Or, Alt, Not };
struct Condition : Node
{
    ConditionKind Kind = ConditionKind::And;
    ValuePtr Id;
    std::vector<ValuePtr> Arguments;
    ValuePtr Output;
    uint32_t Operator = 0;
    std::vector<std::shared_ptr<const Condition>> Children;
    const auto& GetIDNode() const { return Id; }
    const auto& GetArgumentNodes() const { return Arguments; }
    const auto& GetOutputVariableNode() const { return Output; }
    bool HasOutputVariable() const { return Output != nullptr; }
    uint32_t GetOperator() const { return Operator; }
    uint32_t GetOperation() const { return Operator; }
    const auto& GetLeftNode() const { return Arguments[0]; }
    const auto& GetRightNode() const { return Arguments[1]; }
    const auto& GetListNode() const { return Arguments[0]; }
    const auto& GetElementNode() const { return Arguments[1]; }
    const auto& GetRemainderNode() const { return Arguments[2]; }
    const auto& GetSubConditionNodes() const { return Children; }
    const auto& GetSubConditionNode() const { return Children[0]; }
};
using ConditionPtr = std::shared_ptr<const Condition>;

enum class TaskKind : uint8_t { Primitive, Compound, Deferred };
struct Task : Node
{
    TaskKind Kind = TaskKind::Primitive;
    ValuePtr Id;
    std::vector<ValuePtr> Arguments;
    const auto& GetIDNode() const { return Id; }
    const auto& GetArgumentNodes() const { return Arguments; }
    bool IsDeferred() const { return Kind == TaskKind::Deferred; }
};
using TaskPtr = std::shared_ptr<const Task>;

struct Branch : Node
{
    std::string Id;
    ConditionPtr Precondition;
    std::vector<TaskPtr> Tasks;
    const std::string& GetID() const { return Id; }
    const auto& GetPreConditionNode() const { return Precondition; }
    const auto& GetTaskNodes() const { return Tasks; }
};
struct Method : Node
{
    std::string Id;
    std::vector<ValuePtr> Parameters;
    std::vector<std::shared_ptr<const Branch>> Branches;
    bool TopLevel = false;
    bool IsBase = false;
    std::string OverridesDomain;
    const std::string& GetID() const { return Id; }
    const auto& GetParameterNodes() const { return Parameters; }
    const auto& GetBranchNodes() const { return Branches; }
    bool IsTopLevel() const { return TopLevel; }
};
struct Axiom : Node
{
    std::string Id;
    bool IsBase = false;
    std::string OverridesDomain;
    std::vector<ValuePtr> Parameters;
    ConditionPtr Body;
    const std::string& GetID() const { return Id; }
    const auto& GetParameterNodes() const { return Parameters; }
    const auto& GetConditionNode() const { return Body; }
};
struct Constant : Node
{
    std::string Id;
    ValuePtr ValueNode;
    const std::string& GetID() const { return Id; }
    const auto& GetValueNode() const { return ValueNode; }
};
struct ConstantGroup : Node
{
    std::string Id;
    bool IsBase = false;
    std::string OverridesDomain;
    std::vector<std::shared_ptr<const Constant>> Constants;
    const std::string& GetID() const { return Id; }
    const auto& GetConstantNodes() const { return Constants; }
};
struct Domain
{
    std::string Id;
    HTNSourceRange Range;
    uint32_t FileIndex = 0;
    bool IsTopLevel = false;
    bool IsBase = false;
    std::vector<std::shared_ptr<const ConstantGroup>> ConstantGroups;
    std::vector<std::shared_ptr<const Axiom>> Axioms;
    std::vector<std::shared_ptr<const Method>> Methods;
    const std::string& GetID() const { return Id; }
    const auto& GetConstantsNodes() const { return ConstantGroups; }
    const auto& GetAxiomNodes() const { return Axioms; }
    const auto& GetMethodNodes() const { return Methods; }
};
}
