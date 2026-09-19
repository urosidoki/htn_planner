const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const {
    LanguageClient,
    TransportKind
} = require('vscode-languageclient/node');

let client;
let output;
let activeServerPath = '';
let compileDiagnostics;

function isExecutableFile(candidate) {
    try {
        return !!candidate && fs.statSync(candidate).isFile();
    } catch {
        return false;
    }
}

function findWorkspaceServerExecutable() {
    const folders = vscode.workspace.workspaceFolders || [];
    const relativeCandidates = [
        path.join('bin', 'Debug-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer.exe'),
        path.join('bin', 'Profile-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer.exe'),
        path.join('bin', 'ProfileDetailed-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer.exe'),
        path.join('bin', 'Release-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer.exe'),
        path.join('bin', 'Debug-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer'),
        path.join('bin', 'Profile-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer'),
        path.join('bin', 'ProfileDetailed-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer'),
        path.join('bin', 'Release-windows-x86_64', 'HTNLanguageServer', 'HTNLanguageServer')
    ];

    const roots = [];
    for (const folder of folders) {
        let current = folder.uri.fsPath;
        for (let depth = 0; depth < 8; ++depth) {
            roots.push(current);
            const parent = path.dirname(current);
            if (parent === current) break;
            current = parent;
        }
    }

    const activeFile = vscode.window.activeTextEditor?.document?.uri?.fsPath;
    if (activeFile) {
        let current = path.dirname(activeFile);
        for (let depth = 0; depth < 8; ++depth) {
            roots.push(current);
            const parent = path.dirname(current);
            if (parent === current) break;
            current = parent;
        }
    }

    for (const root of [...new Set(roots)]) {
        for (const relative of relativeCandidates) {
            const candidate = path.join(root, relative);
            if (isExecutableFile(candidate)) return candidate;
        }
    }
    return '';
}

function findBundledServerExecutable(context) {
    const platformFolder = process.platform === 'win32' && process.arch === 'x64'
        ? 'win32-x64'
        : `${process.platform}-${process.arch}`;
    const executableName = process.platform === 'win32' ? 'HTNLanguageServer.exe' : 'HTNLanguageServer';
    const candidate = context.asAbsolutePath(path.join('server', platformFolder, executableName));
    return isExecutableFile(candidate) ? candidate : '';
}

function findServerExecutable(context) {
    const bundled = findBundledServerExecutable(context);
    if (bundled) return bundled;

    // The installed extension is intentionally self-contained. The manually configured
    // path is only a fallback when running from source / Extension Development Host,
    // where no bundled server may be present. This also prevents a stale global setting
    // from silently selecting an older server after installing a newer VSIX.
    const configured = vscode.workspace.getConfiguration('htn').get('languageServer.path', '');
    if (isExecutableFile(configured)) return configured;

    return findWorkspaceServerExecutable();
}

async function selectServerExecutable() {
    const result = await vscode.window.showOpenDialog({
        canSelectFiles: true,
        canSelectFolders: false,
        canSelectMany: false,
        openLabel: 'Select HTNLanguageServer',
        filters: process.platform === 'win32' ? { Executable: ['exe'] } : undefined
    });
    if (!result?.length) return;

    await vscode.workspace.getConfiguration('htn').update(
        'languageServer.path',
        result[0].fsPath,
        vscode.ConfigurationTarget.Global
    );

    vscode.window.showInformationMessage('HTN Language Server path saved. Reload VS Code to use it.');
}

function applyCompileDiagnostics(items) {
    if (!compileDiagnostics) return 0;

    compileDiagnostics.clear();
    const grouped = new Map();
    const liveDocumentUris = new Set(
        vscode.workspace.textDocuments
            .filter(document => document.languageId === 'htn')
            .map(document => document.uri.toString()));

    for (const item of items || []) {
        if (!item?.uri || !item?.range || !item?.message) continue;

        let uri;
        try { uri = vscode.Uri.parse(item.uri); } catch { continue; }

        // Open HTN documents already receive the exact same compiler diagnostics through
        // LSP publishDiagnostics. Keeping a second F7 diagnostic collection for them would
        // duplicate every error in Problems. Compile diagnostics are retained for linked
        // source files that are not currently open.
        if (liveDocumentUris.has(uri.toString())) continue;

        const start = new vscode.Position(
            Math.max(0, item.range.start?.line ?? 0),
            Math.max(0, item.range.start?.character ?? 0));
        const end = new vscode.Position(
            Math.max(start.line, item.range.end?.line ?? start.line),
            Math.max(0, item.range.end?.character ?? (start.character + 1)));

        const diagnostic = new vscode.Diagnostic(
            new vscode.Range(start, end),
            item.message,
            vscode.DiagnosticSeverity.Error);
        diagnostic.source = item.source || 'htn-compile';

        const key = uri.toString();
        let entry = grouped.get(key);
        if (!entry) {
            entry = { uri, diagnostics: [] };
            grouped.set(key, entry);
        }
        entry.diagnostics.push(diagnostic);
    }

    for (const { uri, diagnostics } of grouped.values())
        compileDiagnostics.set(uri, diagnostics);

    return [...grouped.values()].reduce((count, entry) => count + entry.diagnostics.length, 0);
}

function appendDiagnosticsToOutput(items) {
    for (const item of items || []) {
        const range = item?.range?.start;
        const location = item?.uri
            ? `${vscode.Uri.parse(item.uri).fsPath}${range ? `(${(range.line ?? 0) + 1},${(range.character ?? 0) + 1})` : ''}`
            : '';
        output.appendLine(`[ERROR] ${location}${location ? ': ' : ''}${item?.message || 'Unknown HTN compile error'}`);
    }
}

async function compileActiveDomain() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'htn') {
        vscode.window.showWarningMessage('Open an HTN .domain file before compiling.');
        return;
    }
    if (!client) {
        vscode.window.showErrorMessage('HTN Language Server is not running.');
        return;
    }

    const filePath = editor.document.uri.fsPath;
    output.appendLine('');
    output.appendLine(`========== HTN Compile: ${filePath} ==========`);

    try {
        const result = await client.sendRequest('htn/compile', {
            textDocument: { uri: editor.document.uri.toString() }
        });

        const message = result?.message || (result?.success ? 'Compile succeeded.' : 'HTN compile failed.');
        const diagnostics = Array.isArray(result?.diagnostics) ? result.diagnostics : [];
        const diagnosticCount = applyCompileDiagnostics(diagnostics);

        if (result?.success) {
            output.appendLine(`[SUCCESS] ${message}`);
            vscode.window.setStatusBarMessage(`HTN: ${message}`, 5000);
            output.appendLine('===============================================');
            output.show(true);
            vscode.window.showInformationMessage(message);
        } else {
            if (diagnostics.length > 0)
                appendDiagnosticsToOutput(diagnostics);
            else
                output.appendLine(`[ERROR] ${message}`);

            output.appendLine('===============================================');

            // Diagnostics for the active/open document are intentionally NOT copied into
            // compileDiagnostics because the Language Server already publishes them live.
            // Therefore diagnosticCount only counts diagnostics for linked files that are
            // not open. Use the compile response itself to decide whether Problems should
            // be focused after a failed compile.
            const hasCompilerDiagnostics = diagnostics.length > 0;
            if (hasCompilerDiagnostics)
                await vscode.commands.executeCommand('workbench.actions.view.problems');
            else
                output.show(true);

            vscode.window.showErrorMessage(
                hasCompilerDiagnostics
                    ? `HTN compile failed with ${diagnostics.length} error(s). See Problems.`
                    : 'HTN compile failed. See Output > HTN for details.');
        }
    } catch (error) {
        const message = error?.message || String(error);
        output.appendLine(`[ERROR] ${message}`);
        output.appendLine('===============================================');
        output.show(true);

        if (message.includes('Method not found: htn/compile')) {
            vscode.window.showErrorMessage(
                `HTN compile is not supported by the running Language Server. ` +
                `VS Code is using: ${activeServerPath || 'unknown server'}. ` +
                `Reinstall/repackage the latest HTN VSIX and reload VS Code.`
            );
            return;
        }
        vscode.window.showErrorMessage('HTN compile failed. See Output > HTN for details.');
    }
}

async function activate(context) {
    output = vscode.window.createOutputChannel('HTN');
    compileDiagnostics = vscode.languages.createDiagnosticCollection('htn-compile');
    context.subscriptions.push(output, compileDiagnostics);

    context.subscriptions.push(vscode.commands.registerCommand('htn.selectLanguageServer', selectServerExecutable));
    context.subscriptions.push(vscode.commands.registerCommand('htn.compileDomain', compileActiveDomain));

    const serverPath = findServerExecutable(context);
    if (!serverPath) {
        const action = await vscode.window.showErrorMessage(
            'HTNLanguageServer was not found. Install a VSIX containing the server or select an executable manually.',
            'Select Server'
        );
        if (action === 'Select Server') await selectServerExecutable();
        return;
    }

    activeServerPath = serverPath;
    output.appendLine(`Using HTNLanguageServer: ${serverPath}`);

    const trace = vscode.workspace.getConfiguration('htn').get('languageServer.trace', false);
    const serverOptions = {
        command: serverPath,
        transport: TransportKind.stdio,
        options: { cwd: vscode.workspace.workspaceFolders?.[0]?.uri.fsPath }
    };

    if (trace) output.appendLine(`Starting HTNLanguageServer: ${serverPath}`);

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'htn' }],
        synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher('**/*.domain') },
        outputChannel: output
    };

    client = new LanguageClient('htnLanguageServer', 'HTN Language Server', serverOptions, clientOptions);
    await client.start();
}

async function deactivate() {
    if (client) {
        await client.stop();
        client = undefined;
    }
}

module.exports = { activate, deactivate };
