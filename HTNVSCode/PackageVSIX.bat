@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-vsix.ps1" %*
if errorlevel 1 (
    echo.
    echo VSIX packaging failed.
    pause
    exit /b 1
)
echo.
echo VSIX packaging completed successfully.
pause
