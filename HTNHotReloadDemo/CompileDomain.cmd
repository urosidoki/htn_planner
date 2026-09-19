@echo off
setlocal
for %%I in ("%~dp0..") do set "HTN_RELOAD_ROOT=%%~fI"
set "HTN_RELOAD_CONFIG=%~1"
if "%HTN_RELOAD_CONFIG%"=="Debug" goto config_ok
if "%HTN_RELOAD_CONFIG%"=="Profile" goto config_ok
if "%HTN_RELOAD_CONFIG%"=="ProfileDetailed" goto config_ok
if "%HTN_RELOAD_CONFIG%"=="Release" goto config_ok
echo Unsupported configuration: %HTN_RELOAD_CONFIG%
exit /b 1
:config_ok
set "HTN_RELOAD_BIN=%HTN_RELOAD_ROOT%\bin\%HTN_RELOAD_CONFIG%-windows-x86_64\HTNHotReloadDemo"
set "HTN_RELOAD_OUT=%HTN_RELOAD_BIN%\candidate"
if not exist "%HTN_RELOAD_OUT%" mkdir "%HTN_RELOAD_OUT%"
if errorlevel 1 exit /b 1
set "HTN_RELOAD_TRANSLATOR=%HTN_RELOAD_ROOT%\bin\%HTN_RELOAD_CONFIG%-windows-x86_64\HTNTranslator\HTNTranslator.exe"
set "HTN_RELOAD_SOURCE=%HTN_RELOAD_ROOT%\Domains\Wanderer.domain"
rem Optional source override is used only by the isolated pipeline self-test.
if not "%~3"=="" set "HTN_RELOAD_SOURCE=%~f3"
if not exist "%HTN_RELOAD_TRANSLATOR%" (
    echo Matching translator not found. Build HTNHotReloadDemo and its dependencies first.
    exit /b 1
)
if not exist "%HTN_RELOAD_BIN%\HTNRuntimeBridge.lib" (
    echo Matching runtime import library not found. Rebuild HTNHotReloadDemo.
    exit /b 1
)
set "HTN_RELOAD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%HTN_RELOAD_VSWHERE%" (
    echo Visual Studio Installer/vswhere not found. Install VS2022 C++ tools.
    exit /b 1
)
rem Invoke directly: FOR /F command substitution adds cmd.exe quoting rules
rem that split an executable path under Program Files. Read its output instead.
set "HTN_RELOAD_VS="
"%HTN_RELOAD_VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%HTN_RELOAD_OUT%\VSInstallation.txt"
if errorlevel 1 exit /b 1
set /p "HTN_RELOAD_VS=" < "%HTN_RELOAD_OUT%\VSInstallation.txt"
if not defined HTN_RELOAD_VS (
    echo VS2022 x64 C++ toolchain not found.
    exit /b 1
)
call "%HTN_RELOAD_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
echo [1/2] Translating "%HTN_RELOAD_SOURCE%" and linked domains...
"%HTN_RELOAD_TRANSLATOR%" "%HTN_RELOAD_SOURCE%" CreateWandererHotReloadHTN "%HTN_RELOAD_OUT%"
if errorlevel 1 exit /b 1
set "HTN_RELOAD_CFLAGS=/O2 /MD"
if "%HTN_RELOAD_CONFIG%"=="Debug" set "HTN_RELOAD_CFLAGS=/Od /MDd"
echo [2/2] Compiling and linking generated C into candidate/WandererHTN.dll...
cl.exe /nologo /TC /std:c11 /W4 /WX /LD %HTN_RELOAD_CFLAGS% %~2 /I"%HTN_RELOAD_ROOT%\HTNFramework\src" /Fo"%HTN_RELOAD_OUT%\Wanderer.obj" "%HTN_RELOAD_OUT%\Wanderer.generated.c" /link /MACHINE:X64 /LIBPATH:"%HTN_RELOAD_BIN%" HTNRuntimeBridge.lib /OUT:"%HTN_RELOAD_OUT%\WandererHTN.dll" /IMPLIB:"%HTN_RELOAD_OUT%\WandererHTN.lib"
if errorlevel 1 exit /b 1
echo Candidate build succeeded. Active DLL has not been changed.
exit /b 0
