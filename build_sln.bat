@echo off
rem ============================================================================
rem  Builds SurveillanceSystem.sln from the command line.
rem
rem    build_sln.bat           Release|x64  ->  bin\Release\SurveillanceSystem.exe
rem    build_sln.bat debug     Debug|x64    ->  bin\Debug\SurveillanceSystem.exe
rem    build_sln.bat gpu       Release with the CUDA 12 build of ONNX Runtime
rem
rem  Needs Visual Studio 2019 (or VS 2022 with the "MSVC v142" toolset) with the
rem  "Desktop development with C++" workload.
rem ============================================================================
setlocal EnableExtensions
set "ROOT=%~dp0"
set "CONFIG=Release"
set "CUDA=false"
set "SETUP_ARGS="
for %%A in (%*) do (
    if /I "%%~A"=="debug" set "CONFIG=Debug"
    if /I "%%~A"=="gpu"   set "CUDA=true"
    if /I "%%~A"=="gpu"   set "SETUP_ARGS=gpu"
)

call "%ROOT%setup_packages.bat" %SETUP_ARGS% || exit /b 1

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="
if exist "%VSWHERE%" (
    rem Prefer Visual Studio 2019 ...
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [16.0^,17.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
    rem ... otherwise any newer Visual Studio that has the v142 toolset installed.
    if not defined MSBUILD for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.v142.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
)
rem ... or the portable VS2019 C++ build tools (MSVC v142 + MSBuild 16) in third_party, if present.
if not defined MSBUILD if exist "%ROOT%third_party\vs2019_buildtools\MSBuild\Current\Bin\amd64\MSBuild.exe" set "MSBUILD=%ROOT%third_party\vs2019_buildtools\MSBuild\Current\Bin\amd64\MSBuild.exe"
if not defined MSBUILD goto :novs

echo Building %CONFIG%^|x64 with "%MSBUILD%"
"%MSBUILD%" "%ROOT%SurveillanceSystem.sln" /m /nologo /v:minimal /p:Configuration=%CONFIG% /p:Platform=x64 /p:SSUseCuda=%CUDA% || goto :fail
echo.
echo Built: %ROOT%bin\%CONFIG%\SurveillanceSystem.exe
exit /b 0

:novs
echo *** Visual Studio 2019 with the C++ workload (MSVC v142) was not found. ***
echo Install it, or open SurveillanceSystem.sln in Visual Studio and build there.
exit /b 1

:fail
echo *** Build failed. See the messages above. ***
exit /b 1
