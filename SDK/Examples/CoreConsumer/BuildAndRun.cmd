@echo off
setlocal
for %%I in ("%~dp0..\..") do set "HTN_PACKAGE_ROOT=%%~fI"
set "HTN_EXAMPLE_CONFIGURATION=%~1"
if "%HTN_EXAMPLE_CONFIGURATION%"=="" set "HTN_EXAMPLE_CONFIGURATION=Release"
if /I "%HTN_EXAMPLE_CONFIGURATION%"=="Release" (
    set "HTN_EXAMPLE_DEFINES=/DHTN_RELEASE"
    set "HTN_EXAMPLE_COMPILE=/MD /O2"
) else if /I "%HTN_EXAMPLE_CONFIGURATION%"=="Debug" (
    set "HTN_EXAMPLE_DEFINES=/DHTN_DEBUG /DHTN_ENABLE_LOGGING /DHTN_VALIDATE_DOMAIN /DHTN_DEBUG_DECOMPOSITION"
    set "HTN_EXAMPLE_COMPILE=/MDd /Od /Z7"
) else (
    echo Expected configuration Release or Debug.
    exit /b 1
)
set "HTN_EXAMPLE_BUILD=%~dp0build\%HTN_EXAMPLE_CONFIGURATION%"
set "HTN_PACKAGE_BIN=%HTN_PACKAGE_ROOT%\bin\windows-x86_64\Release"
set "HTN_PACKAGE_LIB=%HTN_PACKAGE_ROOT%\lib\windows-x86_64\%HTN_EXAMPLE_CONFIGURATION%"

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo Run this script from a Visual Studio 2022 x64 Developer Command Prompt.
    exit /b 1
)
if not exist "%HTN_PACKAGE_BIN%\HTNTranslator.exe" exit /b 1
if not exist "%HTN_PACKAGE_LIB%\HTNFramework.lib" exit /b 1
if not exist "%HTN_EXAMPLE_BUILD%" mkdir "%HTN_EXAMPLE_BUILD%"
if errorlevel 1 exit /b 1

"%HTN_PACKAGE_BIN%\HTNTranslator.exe" "%~dp0example.domain" CreatePackageCoreConsumerHTN "%HTN_EXAMPLE_BUILD%"
if errorlevel 1 exit /b 1
cl.exe /nologo /TC /std:c11 /W4 /WX %HTN_EXAMPLE_COMPILE% %HTN_EXAMPLE_DEFINES% /I"%HTN_PACKAGE_ROOT%\include\HTNFramework" /c "%HTN_EXAMPLE_BUILD%\example.generated.c" /Fo"%HTN_EXAMPLE_BUILD%\domain.obj"
if errorlevel 1 exit /b 1
cl.exe /nologo /std:c++20 /EHsc /W4 /WX %HTN_EXAMPLE_COMPILE% %HTN_EXAMPLE_DEFINES% /I"%HTN_PACKAGE_ROOT%\include\HTNFramework" "%~dp0main.cpp" "%HTN_EXAMPLE_BUILD%\domain.obj" /Fo"%HTN_EXAMPLE_BUILD%\consumer.obj" /Fe"%HTN_EXAMPLE_BUILD%\CoreConsumer.exe" /link /MACHINE:X64 /LIBPATH:"%HTN_PACKAGE_LIB%" HTNFramework.lib
if errorlevel 1 exit /b 1
"%HTN_EXAMPLE_BUILD%\CoreConsumer.exe"
exit /b %ERRORLEVEL%
