// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

// Compiler-side continuations emit C labels, not runtime callbacks. A successful
// condition leaves its checkpoints alive until its caller commits or retries it.
using GeneratedCommit = std::function<void()>;
using GeneratedSuccess = std::function<void(uint32, const GeneratedCommit&)>;

void EmitConditionContinuation(CodeWriter& W, const HTNCompilerIR& B, uint32 inCondition,
                               const BoundVariableSet& inBound, uint32 inFailure,
                               const GeneratedSuccess& inSuccess, const std::string& inDomainSymbol);

void EmitSequenceContinuation(CodeWriter& W, const HTNCompilerIR& B, const ConditionRecord& inAnd,
                              uint32 inOffset, const BoundVariableSet& inBound, uint32 inFailure,
                              const GeneratedSuccess& inSuccess, const std::string& inDomainSymbol)
{
    if (inOffset == inAnd.ChildCount)
    {
        inSuccess(inFailure, [] {});
        return;
    }
    const uint32 Child = B.ConditionChildRefs[inAnd.FirstChildRef + inOffset];
    EmitConditionContinuation(W, B, Child, inBound, inFailure,
        [&](uint32 inRetry, const GeneratedCommit& inCommit) {
            EmitSequenceContinuation(W, B, inAnd, inOffset + 1u,
                AnalyzeCondition(B, Child, inBound).BoundAfter, inRetry,
                [&](uint32 inSuffixRetry, const GeneratedCommit& inSuffixCommit) {
                    inSuccess(inSuffixRetry, [&] { inSuffixCommit(); inCommit(); });
                }, inDomainSymbol);
        }, inDomainSymbol);
}

void EmitConditionContinuation(CodeWriter& W, const HTNCompilerIR& B, const uint32 inCondition,
                               const BoundVariableSet& inBound, const uint32 inFailure,
                               const GeneratedSuccess& inSuccess, const std::string& inDomainSymbol)
{
    if (inCondition == kNoIndex)
    {
        inSuccess(inFailure, [] {});
        return;
    }
    const ConditionRecord& C = B.Conditions[inCondition];
    W.DomainExpressionComment(C.DomainExpression);
    const auto Analysis = AnalyzeCondition(B, inCondition, inBound);
    const uint32 Failure = W.NewLabel();
    const auto Checkpoint = BuildGeneratedConditionCheckpointPlan(B, W.NewLabel(), inCondition, inBound);
    W.Out << "    {\n";
    EmitGeneratedCheckpointDeclarations(W, Checkpoint);
    EmitGeneratedCheckpointPush(W, Checkpoint);
    const bool Composite = C.Kind == HTN_CONDITION_AND || C.Kind == HTN_CONDITION_OR ||
        C.Kind == HTN_CONDITION_ALT || C.Kind == HTN_CONDITION_NOT || C.Kind == HTN_CONDITION_AXIOM;
    const auto Event = [&](bool inBegin, bool inResult) {
        if (!Composite) return;
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_" << (inBegin ? "BEGIN" : "END")
              << "_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition
              << "u, " << (inBegin ? "" : (inResult ? "1, " : "0, ")) << "0);\n";
    };
    Event(true, false);
    const GeneratedSuccess Success = [&](uint32 inRetry, const GeneratedCommit& inCommit) {
        const uint32 Resume = Composite ? W.NewLabel() : inRetry;
        Event(false, true);
        inSuccess(Resume, [&] { inCommit(); EmitGeneratedCheckpointCommit(W, Checkpoint); });
        if (Composite)
        {
            W.Out << W.Label(Resume) << ":;\n";
            Event(true, false);
            W.Out << "    goto " << W.Label(inRetry) << ";\n";
        }
    };

    if (C.Kind == HTN_CONDITION_AND)
        EmitSequenceContinuation(W, B, C, 0u, inBound, Failure, Success, inDomainSymbol);
    else if (C.Kind == HTN_CONDITION_OR || C.Kind == HTN_CONDITION_ALT)
    {
        const bool RuntimeAlt = C.Kind == HTN_CONDITION_ALT &&
            B.RuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled;
        if (RuntimeAlt)
            W.Out << "    int alt_matched_" << Failure << " = 0;\n";
        for (uint32 I = 0; I < C.ChildCount; ++I)
        {
            const uint32 Next = W.NewLabel();
            EmitConditionContinuation(W, B, B.ConditionChildRefs[C.FirstChildRef + I], inBound, Next,
                [&](uint32 inRetry, const GeneratedCommit& inCommit) {
                    if (C.Kind == HTN_CONDITION_OR)
                    {
                        // OR commits to the first success; ALT retains child choices.
                        inCommit();
                        Success(Failure, [] {});
                    }
                    else
                    {
                        if (RuntimeAlt) W.Out << "    alt_matched_" << Failure << " = 1;\n";
                        Success(inRetry, inCommit);
                    }
                }, inDomainSymbol);
            W.Out << W.Label(Next) << ":;\n";
            if (RuntimeAlt)
                W.Out << "    if (alt_matched_" << Failure
                      << " && (context->backtracking_mode & HTN_BACKTRACKING_FACTS_AND_AXIOMS) == 0) goto "
                      << W.Label(Failure) << ";\n";
        }
        W.Out << "    goto " << W.Label(Failure) << ";\n";
    }
    else if (C.Kind == HTN_CONDITION_NOT)
    {
        const uint32 Absent = W.NewLabel();
        if (C.ChildCount)
            EmitConditionContinuation(W, B, B.ConditionChildRefs[C.FirstChildRef], inBound, Absent,
                [&](uint32, const GeneratedCommit& inCommit) {
                    inCommit();
                    W.Out << "    goto " << W.Label(Failure) << ";\n";
                }, inDomainSymbol);
        else W.Out << "    goto " << W.Label(Absent) << ";\n";
        W.Out << W.Label(Absent) << ":;\n";
        Success(Failure, [] {});
    }
    else if (C.Kind == HTN_CONDITION_AXIOM)
    {
        const AxiomRecord& Axiom = B.Axioms[C.ResolvedIndex];
        const std::string Scope = GetGeneratedAxiomScopeName(inCondition) + "_" + std::to_string(Failure);
        const uint32 BodyFailure = W.NewLabel();
        BoundVariableSet Bound;
        for (uint32 I = 0; I < Axiom.ParameterCount; ++I)
        {
            const auto& Parameter = B.Values[Axiom.FirstParameter + I];
            if (B.Strings.Values[Parameter.Text].starts_with("inp_")) Bound.insert(Parameter.Text);
        }
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        EmitGeneratedAxiomBegin(W, B, C, inCondition, inDomainSymbol, Scope);
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        W.Out << "    const uint64_t " << Scope << "_frame = HTN_GENERATED_EXECUTION(context)->current_variable_frame_id;\n";
        EmitConditionContinuation(W, B, Axiom.Condition, Bound, BodyFailure,
            [&](uint32 inRetry, const GeneratedCommit& inCommit) {
                // Suspend the local frame while the caller checks its suffix. END
                // consumes a copy of the scope; the original survives for resumption.
                GeneratedCheckpointPlan Locals;
                Locals.Id = W.NewLabel();
                for (uint32 Word = 0; Word < Axiom.VariableSlotMask.size(); ++Word)
                    for (uint32 Bit = 0; Bit < 64u; ++Bit)
                        if (Axiom.VariableSlotMask[Word] & (uint64_t{1} << Bit))
                            Locals.SavedSlots.push_back(Word * 64u + Bit);
                EmitGeneratedCheckpointDeclarations(W, Locals);
                EmitGeneratedCheckpointPush(W, Locals);
                const uint32 Slots = CountGeneratedAxiomScopeSlots(Axiom);
                W.Out << "    {\n    HTNAtom " << Scope << "_saved_copy[" << std::max(1u, Slots) << "u];\n"
                      << "    HTNAtom " << Scope << "_argument_copy[" << std::max(1u, C.ArgumentCount) << "u];\n"
                      << "    " << inDomainSymbol << "_AXIOM_SCOPE " << Scope << "_scope_copy = " << Scope << ";\n"
                      << "    " << Scope << "_scope_copy.saved_values = " << Scope << "_saved_copy; " << Scope << "_scope_copy.argument_values = " << Scope << "_argument_copy;\n";
                for (uint32 I = 0; I < Slots; ++I)
                    W.Out << "    HTNAtom_Copy(&" << Scope << "_saved_copy[" << I << "u], &" << Scope << ".saved_values[" << I << "u]);\n";
                for (uint32 I = 0; I < C.ArgumentCount; ++I)
                    W.Out << "    HTNAtom_Copy(&" << Scope << "_argument_copy[" << I << "u], &" << Scope << ".argument_values[" << I << "u]);\n";
                const uint32 Resume = W.NewLabel();
                W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n"
                      << "    const int " << Scope << "_valid = " << GetGeneratedAxiomEndHelperName(inDomainSymbol, inCondition)
                      << "(context, 1, &" << Scope << "_scope_copy);\n"
                      << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n"
                      << "    if (!" << Scope << "_valid) goto " << W.Label(Resume) << ";\n";
                Success(Resume, [&] {
                    inCommit();
                    EmitGeneratedCheckpointCommit(W, Locals);
                    W.Out << "    HTNAtom_DestroyRange(" << Scope << ".saved_values, " << Slots << "u);\n"
                          << "    HTNAtom_DestroyRange(" << Scope << ".argument_values, " << C.ArgumentCount << "u);\n";
                });
                W.Out << W.Label(Resume) << ":;\n";
                EmitGeneratedCheckpointRollback(W, Checkpoint);
                EmitGeneratedCheckpointPush(W, Checkpoint);
                EmitGeneratedCheckpointRollback(W, Locals);
                W.Out << "    HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = " << Scope << "_frame;\n"
                      << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_AXIOM(context, &" << inDomainSymbol
                      << "_PLANNER_DEFINITION, " << C.ResolvedIndex << "u);\n"
                      << "    goto " << W.Label(inRetry) << ";\n    }\n";
            }, inDomainSymbol);
        W.Out << W.Label(BodyFailure) << ":;\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        EmitGeneratedAxiomEndCall(W, B, C, inCondition, inDomainSymbol, "0", "    (void)", ";\n", Scope);
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        W.Out << "    goto " << W.Label(Failure) << ";\n";
    }
    else if (C.Kind == HTN_CONDITION_FACT && Analysis.MayProduceMultipleSolutions)
    {
        const uint32 Retry = W.NewLabel();
        const uint32 Next = W.NewLabel();
        W.Out << "    uint32_t fact_choice_cursor_" << Retry << " = 0u;\n" << W.Label(Next) << ":;\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol
              << "_PLANNER_DEFINITION, " << inCondition << "u, 1);\n";
        W.Out << "    if (!" << GetGeneratedFactChoiceHelperName(inDomainSymbol, inCondition)
              << "(context, fact_choice_cursor_" << Retry << "++)) {\n"
              << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol
              << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 1);\n"
              << "    goto " << W.Label(Failure) << ";\n    }\n"
              << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol
              << "_PLANNER_DEFINITION, " << inCondition << "u, 1, 1);\n";
        Success(Retry, [] {});
        W.Out << W.Label(Retry) << ":;\n";
        if (B.RuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled)
            W.Out << "    if ((context->backtracking_mode & HTN_BACKTRACKING_FACTS_AND_AXIOMS) == 0) goto " << W.Label(Failure) << ";\n";
        EmitGeneratedCheckpointRollback(W, Checkpoint);
        EmitGeneratedCheckpointPush(W, Checkpoint);
        W.Out << "    goto " << W.Label(Next) << ";\n";
    }
    else
    {
        const uint32 Matched = W.NewLabel();
        EmitGeneratedConditionLeaf(W, B, inCondition, inBound, Matched, Failure, inDomainSymbol);
        W.Out << W.Label(Matched) << ":;\n";
        Success(Failure, [] {});
    }
    W.Out << W.Label(Failure) << ":;\n";
    EmitGeneratedCheckpointRollback(W, Checkpoint);
    Event(false, false);
    W.Out << "    goto " << W.Label(inFailure) << ";\n    }\n";
}

void EmitLoweredCondition(CodeWriter& W, const HTNCompilerIR& B, uint32 inCondition,
                          const BoundVariableSet& inBound, uint32 inSuccess, uint32 inFailure,
                          const std::string& inDomainSymbol)
{
    EmitConditionContinuation(W, B, inCondition, inBound, inFailure,
        [&](uint32, const GeneratedCommit& inCommit) {
            inCommit();
            W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        }, inDomainSymbol);
}
