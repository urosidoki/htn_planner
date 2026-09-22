// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNCallableSignature.h"
#include "Translator/HTNCompilerDomainValidator.h"

#include "Domain/Diagnostics/HTNDiagnosticSink.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
namespace AST = HTNCompilerAST;

std::string Text(const AST::ValuePtr& inValue)
{
    return inValue ? HTNAtomToString(inValue->GetValue(), false) : std::string{};
}

std::string File(const std::vector<std::string>& inFiles, uint32_t inIndex)
{
    return inIndex < inFiles.size() ? inFiles[inIndex] : std::string{};
}

void Error(HTNDiagnosticSink& outDiagnostics, const std::vector<std::string>& inFiles,
           const AST::Node& inNode, const std::string& inMessage)
{
    outDiagnostics.Error(File(inFiles, inNode.FileIndex), inMessage,
                         HTNDiagnosticRecovery::Recoverable, inNode.Range);
}

void DeclareVariables(const AST::ConditionPtr& inCondition, std::unordered_set<std::string>& ioVariables)
{
    if (!inCondition) return;
    const auto Declare = [&](const AST::ValuePtr& Value)
    {
        if (Value && Value->Kind == AST::ValueKind::Variable)
            ioVariables.emplace(Text(Value));
    };
    switch (inCondition->Kind)
    {
    case AST::ConditionKind::Fact:
    case AST::ConditionKind::Axiom:
        for (const auto& Argument : inCondition->Arguments) Declare(Argument);
        break;
    case AST::ConditionKind::Call:
        Declare(inCondition->Output);
        break;
    case AST::ConditionKind::Split:
        if (inCondition->Arguments.size() == 3u)
        {
            Declare(inCondition->Arguments[1]);
            Declare(inCondition->Arguments[2]);
        }
        break;
    default:
        for (const auto& Child : inCondition->Children) DeclareVariables(Child, ioVariables);
        break;
    }
}

bool ValidateValueUse(const AST::ValuePtr& inValue,
                      const std::unordered_set<std::string>& inVariables,
                      const std::string& inUsage,
                      const std::vector<std::string>& inFiles,
                      HTNDiagnosticSink& outDiagnostics)
{
    if (!inValue) return true;
    if (inValue->Kind == AST::ValueKind::Variable && !inVariables.contains(Text(inValue)))
    {
        Error(outDiagnostics, inFiles, *inValue,
              "Unknown variable '" + Text(inValue) + "' used by " + inUsage + ".");
        return false;
    }
    bool Valid = true;
    if (inValue->Kind == AST::ValueKind::Call)
        for (const auto& Argument : inValue->CallArguments)
            Valid = ValidateValueUse(Argument, inVariables,
                                     "call expression '" + Text(inValue->CallId) + "'",
                                     inFiles, outDiagnostics) && Valid;
    if (inValue->Kind == AST::ValueKind::Arithmetic)
        for (const auto& Operand : inValue->GetArithmeticOperandNodes())
            Valid = ValidateValueUse(Operand, inVariables, inUsage, inFiles, outDiagnostics) && Valid;
    return Valid;
}

bool ValidateConditionVariables(const AST::ConditionPtr& inCondition,
                                const std::unordered_set<std::string>& inVariables,
                                std::unordered_set<std::string>& ioSingletons,
                                const std::vector<std::string>& inFiles,
                                HTNDiagnosticSink& outDiagnostics)
{
    if (!inCondition) return true;
    bool Valid = true;
    const auto CheckSingletonUse = [&](const AST::ValuePtr& Value, const std::string& Usage)
    {
        if (Value && Value->Kind == AST::ValueKind::Variable && Text(Value).starts_with("any_"))
        {
            Error(outDiagnostics, inFiles, *Value,
                  "Singleton variable '?" + Text(Value) +
                  "' may only be used as a fact argument; it is never bound and cannot be consumed by " + Usage + ".");
            Valid = false;
        }
    };
    switch (inCondition->Kind)
    {
    case AST::ConditionKind::Fact:
        for (const auto& Argument : inCondition->Arguments)
            if (Argument->Kind == AST::ValueKind::Variable && Text(Argument).starts_with("any_") &&
                !ioSingletons.emplace(Text(Argument)).second)
            {
                Error(outDiagnostics, inFiles, *Argument,
                      "Singleton variable '?" + Text(Argument) + "' may only appear once in the same scope.");
                Valid = false;
            }
        break;
    case AST::ConditionKind::Call:
        for (const auto& Argument : inCondition->Arguments)
        {
            CheckSingletonUse(Argument, "a callterm");
            Valid = ValidateValueUse(Argument, inVariables, "callterm '" + Text(inCondition->Id) + "'",
                                     inFiles, outDiagnostics) && Valid;
        }
        CheckSingletonUse(inCondition->Output, "a callterm output");
        break;
    case AST::ConditionKind::Comparison:
    case AST::ConditionKind::Split:
        for (size_t I = 0; I < inCondition->Arguments.size(); ++I)
        {
            const std::string Usage = inCondition->Kind == AST::ConditionKind::Comparison
                ? "a built-in comparison" : (I == 0u ? "split_list input" : "split_list output");
            CheckSingletonUse(inCondition->Arguments[I], Usage);
            Valid = ValidateValueUse(inCondition->Arguments[I], inVariables, Usage,
                                     inFiles, outDiagnostics) && Valid;
        }
        break;
    default:
        for (const auto& Child : inCondition->Children)
            Valid = ValidateConditionVariables(Child, inVariables, ioSingletons,
                                               inFiles, outDiagnostics) && Valid;
        break;
    }
    return Valid;
}

bool ValidateMethodVariables(const AST::Method& inMethod,
                             const std::vector<std::string>& inFiles,
                             HTNDiagnosticSink& outDiagnostics)
{
    std::unordered_set<std::string> Parameters;
    for (const auto& Parameter : inMethod.Parameters) Parameters.emplace(Text(Parameter));
    bool Valid = true;
    for (const auto& Branch : inMethod.Branches)
    {
        std::unordered_set<std::string> Variables = Parameters;
        std::unordered_set<std::string> Singletons;
        DeclareVariables(Branch->Precondition, Variables);
        Valid = ValidateConditionVariables(Branch->Precondition, Variables, Singletons,
                                           inFiles, outDiagnostics) && Valid;
        for (const auto& Task : Branch->Tasks)
        {
            const std::string Usage = std::string(Task->Kind == AST::TaskKind::Primitive ? "primitive task '" :
                (Task->Kind == AST::TaskKind::Deferred ? "deferred call '" : "compound task '")) + Text(Task->Id) + "'";
            for (const auto& Argument : Task->Arguments)
            {
                if (Argument->Kind == AST::ValueKind::Variable && Text(Argument).starts_with("any_"))
                {
                    Error(outDiagnostics, inFiles, *Argument,
                          "Singleton variable '?" + Text(Argument) +
                          "' may only be used as a fact argument; it is never bound and cannot be consumed by " + Usage + ".");
                    Valid = false;
                }
                Valid = ValidateValueUse(Argument, Variables, Usage, inFiles, outDiagnostics) && Valid;
            }
        }
    }
    return Valid;
}

void CollectAxiomCalls(const AST::ConditionPtr& inCondition, std::vector<std::string>& outCalls)
{
    if (!inCondition) return;
    if (inCondition->Kind == AST::ConditionKind::Axiom)
        outCalls.push_back(HTNCallableSignature(Text(inCondition->Id), inCondition->Arguments.size()));
    for (const auto& Child : inCondition->Children)
        CollectAxiomCalls(Child, outCalls);
}

bool ValidateAxiomCycles(const AST::Domain& inDomain,
                         const std::vector<std::string>& inFiles,
                         HTNDiagnosticSink& outDiagnostics)
{
    std::unordered_map<std::string, size_t> IndexById;
    for (size_t I = 0; I < inDomain.Axioms.size(); ++I)
        IndexById.emplace(HTNCallableSignature(inDomain.Axioms[I]->Id, inDomain.Axioms[I]->Parameters.size()), I);
    std::vector<uint8_t> States(inDomain.Axioms.size(), 0u);
    std::vector<size_t> Stack;
    std::function<bool(size_t)> Visit = [&](size_t Index)
    {
        States[Index] = 1u;
        Stack.push_back(Index);
        std::vector<std::string> Calls;
        CollectAxiomCalls(inDomain.Axioms[Index]->Body, Calls);
        for (const std::string& Call : Calls)
        {
            const auto It = IndexById.find(Call);
            if (It == IndexById.end()) continue;
            if (States[It->second] == 0u)
            {
                if (!Visit(It->second)) return false;
            }
            else if (States[It->second] == 1u)
            {
                const auto Begin = std::find(Stack.begin(), Stack.end(), It->second);
                std::vector<std::string> Cycle;
                for (auto Item = Begin; Item != Stack.end(); ++Item)
                    Cycle.push_back(HTNCallableSignature(inDomain.Axioms[*Item]->Id, inDomain.Axioms[*Item]->Parameters.size()));
                const auto Smallest = std::min_element(Cycle.begin(), Cycle.end());
                std::rotate(Cycle.begin(), Smallest, Cycle.end());
                std::ostringstream Message;
                Message << "Cyclic axiom dependency: ";
                for (const auto& Id : Cycle) Message << Id << " -> ";
                Message << Cycle.front();
                Error(outDiagnostics, inFiles, *inDomain.Axioms[Index], Message.str());
                return false;
            }
        }
        Stack.pop_back();
        States[Index] = 2u;
        return true;
    };
    for (size_t I = 0; I < inDomain.Axioms.size(); ++I)
        if (States[I] == 0u && !Visit(I)) return false;
    return true;
}

std::string DeclarationKey(const AST::Method& inMethod)
{
    return HTNCallableSignature(inMethod.Id, inMethod.Parameters.size());
}

std::string DeclarationKey(const AST::Axiom& inAxiom)
{
    return HTNCallableSignature(inAxiom.Id, inAxiom.Parameters.size());
}

template <typename T>
bool ValidateOverride(const AST::Domain& inModule,
                      const std::shared_ptr<const T>& inDeclaration,
                      const char* inKind,
                      const std::unordered_map<std::string, bool>& inBaseDomains,
                      std::unordered_map<std::string, std::shared_ptr<const T>>& ioQualified,
                      std::unordered_map<std::string, std::string>& ioEffectiveOwner,
                      std::unordered_map<std::string, bool>& ioSpecialization,
                      const std::vector<std::string>& inFiles,
                      HTNDiagnosticSink& outDiagnostics)
{
    const std::string Key = DeclarationKey(*inDeclaration);
    const std::string QualifiedId = inModule.Id + "::" + Key;
    if (inDeclaration->IsBase && !inModule.IsBase)
    {
        Error(outDiagnostics, inFiles, *inDeclaration,
              std::string(inKind) + " '" + QualifiedId + "' is declared base but domain '" +
              inModule.Id + "' is not a base domain");
        return false;
    }
    if (ioQualified.contains(QualifiedId))
    {
        Error(outDiagnostics, inFiles, *inDeclaration,
              "Duplicate " + std::string(inKind) + " declaration '" + QualifiedId + "'");
        return false;
    }
    if (!inDeclaration->OverridesDomain.empty())
    {
        const std::string BaseQualifiedId = inDeclaration->OverridesDomain + "::" + Key;
        const auto Base = ioQualified.find(BaseQualifiedId);
        const auto BaseDomain = inBaseDomains.find(inDeclaration->OverridesDomain);
        if (Base == ioQualified.end() || BaseDomain == inBaseDomains.end() ||
            !BaseDomain->second || !ioSpecialization[BaseQualifiedId] ||
            Base->second->Parameters.size() != inDeclaration->Parameters.size() ||
            ioEffectiveOwner[Key] != inDeclaration->OverridesDomain)
        {
            Error(outDiagnostics, inFiles, *inDeclaration,
                  "Invalid " + std::string(inKind) + " override '" + QualifiedId +
                  "' of '" + BaseQualifiedId + "'");
            return false;
        }
        ioSpecialization[QualifiedId] = inModule.IsBase;
    }
    else
    {
        if (ioEffectiveOwner.contains(Key))
        {
            Error(outDiagnostics, inFiles, *inDeclaration,
                  std::string(inKind) + " '" + inDeclaration->Id + "' already exists in domain '" +
                  ioEffectiveOwner[Key] + "'");
            return false;
        }
        ioSpecialization[QualifiedId] = inDeclaration->IsBase;
    }
    ioQualified.emplace(QualifiedId, inDeclaration);
    ioEffectiveOwner[Key] = inModule.Id;
    return true;
}

bool ValidateOverrideChains(const std::vector<AST::Domain>& inModules,
                            const std::vector<std::string>& inFiles,
                            HTNDiagnosticSink& outDiagnostics)
{
    std::unordered_map<std::string, bool> BaseDomains;
    for (const auto& Module : inModules) BaseDomains.emplace(Module.Id, Module.IsBase);
    std::unordered_map<std::string, std::shared_ptr<const AST::Constant>> QualifiedConstants;
    std::unordered_map<std::string, std::string> EffectiveConstantOwner;
    std::unordered_map<std::string, bool> ConstantSpecialization;
    std::unordered_map<std::string, std::shared_ptr<const AST::Axiom>> QualifiedAxioms;
    std::unordered_map<std::string, std::string> EffectiveAxiomOwner;
    std::unordered_map<std::string, bool> AxiomSpecialization;
    std::unordered_map<std::string, std::shared_ptr<const AST::Method>> QualifiedMethods;
    std::unordered_map<std::string, std::string> EffectiveMethodOwner;
    std::unordered_map<std::string, bool> MethodSpecialization;
    bool Valid = true;

    for (const AST::Domain& Module : inModules)
    {
        for (const auto& Group : Module.ConstantGroups)
        {
            if (Group->IsBase && !Module.IsBase)
            {
                Error(outDiagnostics, inFiles, *Group,
                      "Constants block '" + Module.Id + "::" + Group->Id +
                      "' is declared base but domain '" + Module.Id + "' is not a base domain");
                Valid = false;
                continue;
            }
            if (!Group->OverridesDomain.empty() &&
                (!BaseDomains.contains(Group->OverridesDomain) ||
                 !BaseDomains.find(Group->OverridesDomain)->second))
            {
                Error(outDiagnostics, inFiles, *Group,
                      "Constants block '" + Module.Id + "::" + Group->Id +
                      "' overrides missing or non-base domain '" + Group->OverridesDomain + "'");
                Valid = false;
                continue;
            }
            for (const auto& Constant : Group->Constants)
            {
                const std::string QualifiedId = Module.Id + "::" + Constant->Id;
                if (Group->OverridesDomain.empty())
                {
                    if (EffectiveConstantOwner.contains(Constant->Id))
                    {
                        Error(outDiagnostics, inFiles, *Constant,
                              "Constant '" + Constant->Id + "' already exists in domain '" +
                              EffectiveConstantOwner[Constant->Id] + "'");
                        Valid = false;
                        continue;
                    }
                    ConstantSpecialization[QualifiedId] = Group->IsBase;
                }
                else
                {
                    const std::string BaseQualifiedId = Group->OverridesDomain + "::" + Constant->Id;
                    if (!QualifiedConstants.contains(BaseQualifiedId) ||
                        !ConstantSpecialization[BaseQualifiedId] ||
                        EffectiveConstantOwner[Constant->Id] != Group->OverridesDomain)
                    {
                        Error(outDiagnostics, inFiles, *Constant,
                              "Invalid constant override '" + QualifiedId + "' of '" + BaseQualifiedId + "'");
                        Valid = false;
                        continue;
                    }
                    ConstantSpecialization[QualifiedId] = Module.IsBase;
                }
                QualifiedConstants.emplace(QualifiedId, Constant);
                EffectiveConstantOwner[Constant->Id] = Module.Id;
            }
        }
        for (const auto& Axiom : Module.Axioms)
            Valid = ValidateOverride(Module, Axiom, "Axiom", BaseDomains, QualifiedAxioms,
                                     EffectiveAxiomOwner, AxiomSpecialization, inFiles, outDiagnostics) && Valid;
        for (const auto& Method : Module.Methods)
            Valid = ValidateOverride(Module, Method, "Method", BaseDomains, QualifiedMethods,
                                     EffectiveMethodOwner, MethodSpecialization, inFiles, outDiagnostics) && Valid;
    }
    return Valid;
}
}

bool HTNValidateCompilerDomainModules(const std::vector<HTNCompilerAST::Domain>& inModules,
                                      const std::vector<std::string>& inSourceFiles,
                                      bool inRequireTopLevelRoot,
                                      HTNDiagnosticSink& outDiagnostics)
{
    if (inModules.empty()) return false;
    namespace AST = HTNCompilerAST;
    bool Valid = true;
    const AST::Domain& Root = inModules.back();
    if (inRequireTopLevelRoot && !Root.IsTopLevel)
    {
        outDiagnostics.Error(File(inSourceFiles, Root.FileIndex), "Root domain must be top_level_domain",
                             HTNDiagnosticRecovery::Recoverable, Root.Range);
        Valid = false;
    }
    std::unordered_set<std::string> DomainIds;
    size_t TopLevelMethods = 0;
    for (const AST::Domain& Module : inModules)
    {
        if (!DomainIds.emplace(Module.Id).second)
        {
            outDiagnostics.Error(File(inSourceFiles, Module.FileIndex),
                                 "Duplicate domain id '" + Module.Id + "'", HTNDiagnosticRecovery::Dependent, Module.Range);
            Valid = false;
        }
        if (&Module != &Root && Module.IsTopLevel)
        {
            outDiagnostics.Error(File(inSourceFiles, Module.FileIndex),
                                 "Included domain cannot be top_level_domain",
                                 HTNDiagnosticRecovery::Recoverable, Module.Range);
            Valid = false;
        }
        for (const auto& Method : Module.Methods)
        {
            if (Method->TopLevel) ++TopLevelMethods;
            if (&Module != &Root && Method->TopLevel)
            {
                Error(outDiagnostics, inSourceFiles, *Method,
                      "Included domain '" + Module.Id + "' cannot declare top_level_method '" + Method->Id + "'");
                Valid = false;
            }
            for (const auto& Parameter : Method->Parameters)
                if (!Text(Parameter).starts_with("inp_"))
                {
                    Error(outDiagnostics, inSourceFiles, *Parameter,
                          "Method '" + Method->Id + "' parameter '?" + Text(Parameter) + "' must use the inp_ prefix");
                    Valid = false;
                }
            Valid = ValidateMethodVariables(*Method, inSourceFiles, outDiagnostics) && Valid;
        }
        for (const auto& Axiom : Module.Axioms)
            for (const auto& Parameter : Axiom->Parameters)
                if (!Text(Parameter).starts_with("inp_") && !Text(Parameter).starts_with("out_") &&
                    !Text(Parameter).starts_with("io_"))
                {
                    Error(outDiagnostics, inSourceFiles, *Parameter,
                          "Axiom '" + Axiom->Id + "' parameter '?" + Text(Parameter) +
                          "' must use an inp_, out_ or io_ prefix");
                    Valid = false;
                }
    }
    if (inRequireTopLevelRoot && Root.IsTopLevel && TopLevelMethods == 0u)
    {
        outDiagnostics.Error(File(inSourceFiles, Root.FileIndex),
                             "Root top_level_domain has no top_level_method",
                             HTNDiagnosticRecovery::Recoverable, Root.Range);
        Valid = false;
    }
    Valid = ValidateOverrideChains(inModules, inSourceFiles, outDiagnostics) && Valid;

    std::unordered_map<std::string, std::shared_ptr<const AST::Method>> Methods;
    for (const AST::Domain& Module : inModules)
        for (const auto& Method : Module.Methods)
        {
            Methods[HTNCallableSignature(Module.Id + "::" + Method->Id, Method->Parameters.size())] = Method;
            Methods[HTNCallableSignature(Method->Id, Method->Parameters.size())] = Method;
        }
    for (const AST::Domain& Module : inModules)
        for (const auto& Method : Module.Methods)
            for (const auto& Branch : Method->Branches)
                for (const auto& Task : Branch->Tasks)
                {
                    if (Task->Kind == AST::TaskKind::Primitive) continue;
                    const std::string Id = Text(Task->Id);
                    const auto It = Methods.find(HTNCallableSignature(Id, Task->Arguments.size()));
                    if (It == Methods.end())
                    {
                        Error(outDiagnostics, inSourceFiles, *Task,
                              std::string(Task->Kind == AST::TaskKind::Deferred ? "Deferred method call '" :
                                  "Compound method call '") + Id + "' cannot be resolved at link time: no overload accepts " +
                              std::to_string(Task->Arguments.size()) + " argument(s)");
                        Valid = false;
                    }

                }

    AST::Domain Linked;
    for (const AST::Domain& Module : inModules)
        for (const auto& Axiom : Module.Axioms)
        {
            auto Qualified = std::make_shared<AST::Axiom>(*Axiom);
            Qualified->Id = Module.Id + "::" + Axiom->Id;
            Linked.Axioms.push_back(std::move(Qualified));
        }
    std::unordered_map<std::string, std::shared_ptr<const AST::Axiom>> Effective;
    for (const AST::Domain& Module : inModules)
        for (const auto& Axiom : Module.Axioms) Effective[HTNCallableSignature(Axiom->Id, Axiom->Parameters.size())] = Axiom;
    for (const AST::Domain& Module : inModules)
        for (const auto& Axiom : Module.Axioms)
            if (Effective[HTNCallableSignature(Axiom->Id, Axiom->Parameters.size())] == Axiom) Linked.Axioms.push_back(Axiom);
    std::unordered_set<std::string> AxiomSignatures;
    for (const auto& Axiom : Linked.Axioms)
        AxiomSignatures.emplace(HTNCallableSignature(Axiom->Id, Axiom->Parameters.size()));
    std::function<void(const AST::ConditionPtr&)> ValidateAxiomCall = [&](const AST::ConditionPtr& Condition)
    {
        if (!Condition) return;
        if (Condition->Kind == AST::ConditionKind::Axiom &&
            !AxiomSignatures.contains(HTNCallableSignature(Text(Condition->Id), Condition->Arguments.size())))
        {
            Error(outDiagnostics, inSourceFiles, *Condition,
                  "Axiom call '" + Text(Condition->Id) + "' cannot be resolved at link time: no overload accepts " +
                  std::to_string(Condition->Arguments.size()) + " argument(s)");
            Valid = false;
        }
        for (const auto& Child : Condition->Children) ValidateAxiomCall(Child);
    };
    for (const auto& Module : inModules)
    {
        for (const auto& Method : Module.Methods)
            for (const auto& Branch : Method->Branches) ValidateAxiomCall(Branch->Precondition);
        for (const auto& Axiom : Module.Axioms) ValidateAxiomCall(Axiom->Body);
    }
    Valid = ValidateAxiomCycles(Linked, inSourceFiles, outDiagnostics) && Valid;
    return Valid && !outDiagnostics.HasErrors();
}
