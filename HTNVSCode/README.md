# HTN Language Support

VS Code language support for HTN `.domain` files.

## Installation

Install the generated `.vsix` once. The packaged extension contains `HTNLanguageServer.exe`, so a normal user does not need to build or configure the server separately.

In VS Code use **Extensions: Install from VSIX...**, or from a terminal:

```powershell
code --install-extension .\htn-language-support-<version>-win32-x64.vsix --force
```

Then open any `.domain` file. Diagnostics, completion and Go to Definition start automatically.

## Compile active domain

Press **F7** or run **HTN: Compile Active Domain** from the Command Palette. This uses the same compile semantics as HTNEditor: the active document is compiled from its current in-memory buffer and open/dirty include files are also read from their in-memory buffers before falling back to disk.

## Building the distributable VSIX

The easiest option on Windows is to double-click `PackageVSIX.bat`.

Or from `HTNVSCode` run:

```powershell
.\package-vsix.ps1
```

The script builds `HTNLanguageServer` in `Release x64`, copies it into the extension, installs npm dependencies and generates a platform-specific `win32-x64` VSIX.

You can also use:

```powershell
npm run package:vsix
```

For a debug server package:

```powershell
.\package-vsix.ps1 -Configuration Debug
```

`htn.languageServer.path` remains available as an optional manual override for development.


## Server selection

Installed VSIX packages always prefer the bundled `server/win32-x64/HTNLanguageServer.exe`. A configured `htn.languageServer.path` is only used as a fallback when no bundled server exists (for example while developing the extension from source). This prevents an old globally configured server from being used accidentally after installing a newer extension.

The `HTN` Output channel prints the exact Language Server executable selected at startup.


## Compile output

Press **F7** to compile the active domain. Compilation failures automatically open the **HTN** output channel and print the complete error message there. You can reopen it at any time from **View > Output > HTN**.

## Compile diagnostics (0.4.0)

Press **F7** to compile the active `.domain`. Compile errors are returned as structured diagnostics, shown as red errors in VS Code's **Problems** panel, and underlined in the editor. The tolerant semantic pass reports all independently detectable unresolved variables, constants, methods, structural errors, and override errors in one compile instead of stopping at the first one. Strict linker/loader errors that cannot yet be recovered from are still reported as a compile diagnostic.
