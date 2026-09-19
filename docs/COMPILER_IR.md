# Compiler pipeline and IR boundary

The generated planner follows this sequence:

```mermaid
flowchart LR
    Source["Domain source text"] --> Lexer["HTNCompilerDomainLexer"]
    Lexer --> Tokens["HTNToken sequence"]
    Tokens --> Parser["HTNCompilerDomainSyntaxParser"]
    Parser --> AST["HTNCompilerAST"]
    AST --> Validator["Validation and linking"]
    Validator --> Builder["HTNCompilerIRBuilder"]
    Builder --> IR["HTNCompilerIR"]
    IR --> Generator["HTNCCodeGenerator"]
    Generator --> C["Generated C"]
    C --> Native["Native planner definition"]
```

Each stage has one responsibility:

1. `HTNCompilerDomainLexer` converts characters into tokens and records source
   ranges. It does not assign domain meaning to the token sequence.
2. `HTNCompilerDomainSyntaxParser` converts tokens into compiler-owned values,
   conditions, tasks and declarations.
3. `HTNCompilerDomainValidator` and `HTNCompilerDomainLoader` validate declarations,
   traverse includes and select effective and qualified declarations in include order.
4. `HTNCompilerIRBuilder` resolves compile-time references and lowers the linked AST
   into the representation needed by code generation.
5. `HTNCCodeGenerator` emits C and static metadata from the completed IR.
6. The platform C compiler produces the native planner definition consumed by the
   runtime.

The lexer, AST and IR belong to the compiler frontend. They are build-time data and
are not retained by a generated planner while it executes.

`HTNCompilerIRBuilder.cpp` traverses only `HTNCompilerAST`. It lowers the
compiler syntax, resolves compile-time references and validates the IR.
`HTNCompilerDomainLoader` owns the compiler result API for disk and in-memory
sources. `HTNTranslateDomain` consumes that loader, so its path never handles
frontend nodes or declaration pointer maps. Compiler loads use the compiler
parser, include traversal, semantic validator and linker without building
frontend nodes. The compiler parser and validator report their own diagnostics,
including multiple independent declaration errors. The older parser is no
longer called by the generated loader, which lives in
`Translator/HTNCompilerDomainLoader.cpp`.

The generated frontend owns `HTNCompilerDomainLexer` and
`HTNCompilerDomainLexerContext`. Generic token and lexer primitives remain under
`Parser`, where they are also used by the world-state frontend. The SDK build contains
the compiler frontend and omits the legacy domain parser, loader, nodes and semantic
tooling. Building its translator verifies that compiler translation has no dependency
on those removed implementations.

`HTNCCodeGenerator.cpp` emits C from the completed IR. Its public API and options
do not expose frontend nodes. The IR owns
its tables and strings, so it does not retain AST nodes or source-text views.
`HTNCompilerOptions.h` holds options shared by the builder and emitter.

The IR is an internal compiler representation, not a serialized or public SDK
contract. Source locations now come directly from AST ranges. The loader tracks
which input file owns each linked declaration, including qualified declarations
from includes, and the IR carries that file index with line and column ranges.
Generated debug metadata preserves those ranges for debugger nodes. This changes
the layout of debug metadata, so the debug planner ABI versions advance to
`0x48550002` (debug) and `0x48570002` (debug with profiling). Plain and
profiling builds without debug retain their ABI versions.

The same source ranges are emitted as optional generated debug metadata. When
`HTN_DEBUG_DECOMPOSITION` is enabled, generated execution events use that metadata to
recover domain locations and display names. See [generated debugger](GENERATED_DEBUGGER.md).

Validation covers Windows Profile/x64, the SDK variant matrix, compiler architecture
tests, generated domain execution and dynamic module loading.
