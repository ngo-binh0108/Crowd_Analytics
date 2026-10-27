@echo off
rem ============================================================================
rem  Surveillance System - alternative CMake route (requires Visual Studio 2019)
rem
rem  The simplest way is to open SurveillanceSystem.sln directly. This script
rem  instead generates a CMake-based solution in .\build.
rem
rem    build_vs2019.bat            extract packages + generate build\SurveillanceSystem.sln
rem    build_vs2019.bat gpu        same, but link the CUDA 12 build of ONNX Runtime
rem    build_vs2019.bat build      also compile Release from the command line
rem    build_vs2019.bat gpu build  both
rem ============================================================================
setlocal EnableExtensions
set "ROOT=%~dp0"
set "USE_CUDA=OFF"
set "DO_BUILD=0"
set "SETUP_ARGS="
for %%A in (%*) do (
    if /I "%%~A"=="gpu"   set "USE_CUDA=ON"
    if /I "%%~A"=="gpu"   set "SETUP_ARGS=gpu"
    if /I "%%~A"=="build" set "DO_BUILD=1"
)

call "%ROOT%setup_packages.bat" %SETUP_ARGS% || goto :fail
set "CMAKE_EXE=%ROOT%third_party\cmake-4.4.3-windows-x86_64\bin\cmake.exe"

rem Use an installed Visual Studio 2019 if there is one; otherwise the portable
rem VS2019 C++ build tools in third_party (not registered with the VS installer).
set "VS_INSTANCE="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "HAVE_VS2019="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [16.0^,17.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "HAVE_VS2019=%%i"
set "PORTABLE=%ROOT%third_party\vs2019_buildtools"
if not defined HAVE_VS2019 if exist "%PORTABLE%\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_INSTANCE=-T host=x64 "-DCMAKE_GENERATOR_INSTANCE=%PORTABLE%,version=16.11.37627.13""
    echo Using the portable VS2019 build tools in third_party\vs2019_buildtools
)

echo Generating Visual Studio 2019 solution (CUDA=%USE_CUDA%)...
"%CMAKE_EXE%" -S "%ROOT%." -B "%ROOT%build" -G "Visual Studio 16 2019" -A x64 %VS_INSTANCE% -DSS_USE_CUDA=%USE_CUDA% || goto :fail

if "%DO_BUILD%"=="1" (
    "%CMAKE_EXE%" --build "%ROOT%build" --config Release --parallel || goto :fail
    echo.
    echo Built: %ROOT%build\Release\SurveillanceSystem.exe
)

echo.
echo Done. Open "%ROOT%build\SurveillanceSystem.sln" in Visual Studio 2019,
echo select Release or Debug and press F5.
exit /b 0

:fail
echo.
echo *** Build preparation failed. See the messages above. ***
exit /b 1
