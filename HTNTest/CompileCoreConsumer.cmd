@echo off
setlocal
rem Run from a VS2022 x64 Developer Command Prompt after a DEFAULT Release SDK build.
for %%I in ("%~dp0..") do set "HTN_CORE_ROOT=%%~fI"
set "HTN_CORE_BIN=%HTN_CORE_ROOT%\bin\sdk\Release-windows-x86_64"
set "HTN_CORE_OUT=%HTN_CORE_BIN%\CoreConsumer"
if not exist "%HTN_CORE_BIN%\HTNTranslator\HTNTranslator.exe" (
    echo Build HTNSDK.sln in Release/x64 first, with default Premake options.
    exit /b 1
)
if not exist "%HTN_CORE_BIN%\HTNFramework\HTNFramework.lib" exit /b 1
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo Run from a VS2022 x64 Developer Command Prompt.
    exit /b 1
)
if not exist "%HTN_CORE_OUT%" mkdir "%HTN_CORE_OUT%"
if errorlevel 1 exit /b 1
"%HTN_CORE_BIN%\HTNTranslator\HTNTranslator.exe" "%HTN_CORE_ROOT%\Domains\Test\backtracking_policy.domain" CreateCoreConsumerHTN "%HTN_CORE_OUT%"
if errorlevel 1 exit /b 1
cl.exe /nologo /TC /std:c11 /W4 /WX /MD /O2 /DHTN_RELEASE /I"%HTN_CORE_ROOT%\HTNFramework\src" /c "%HTN_CORE_OUT%\backtracking_policy.generated.c" /Fo"%HTN_CORE_OUT%\domain.obj"
if errorlevel 1 exit /b 1
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /DHTN_RELEASE /I"%HTN_CORE_ROOT%\HTNFramework\src" "%HTN_CORE_ROOT%\HTNTest\CoreConsumer\main.cpp" "%HTN_CORE_OUT%\domain.obj" /Fo"%HTN_CORE_OUT%\consumer.obj" /Fe"%HTN_CORE_OUT%\CoreConsumer.exe" /link /MACHINE:X64 /LIBPATH:"%HTN_CORE_BIN%\HTNFramework" HTNFramework.lib
if errorlevel 1 exit /b 1
"%HTN_CORE_OUT%\CoreConsumer.exe"
exit /b %errorlevel%
