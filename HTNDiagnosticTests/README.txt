HTN VS Code diagnostic tests

Extract this folder OUTSIDE the HTN repository's Domains directory.

Recommended location:
C:\Development\HTNDiagnosticTests\

Open C:\Development\HTNDiagnosticTests in VS Code and test:

1. DiagnosticSemanticErrors.domain
   - Multiple semantic errors should appear at their real source positions.

2. DiagnosticParserRecovery.domain
   - One intentional parser error plus later semantic errors should all be reported.
   - The parser error should be around line 4.
   - Semantic errors should point to the actual task/method calls.

3. DiagnosticRootWithInclude.domain
   - Tests diagnostics across an included file.
   - Open Includes\DiagnosticBrokenInclude.domain too.
   - Modify the include without saving, then compile the root with F7.
   - The language server should use the open buffer.

These files are intentionally invalid. Do NOT copy them into the repository's Domains folder,
because the build automatically runs HTNTranslator over domains found there.
