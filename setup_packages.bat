@echo off
rem ============================================================================
rem  Extracts the third-party packages from .\package into .\third_party
rem  (needed once before building SurveillanceSystem.sln or the CMake project).
rem
rem    setup_packages.bat        CPU build of ONNX Runtime
rem    setup_packages.bat gpu    also extract the CUDA 12 build of ONNX Runtime
rem ============================================================================
setlocal EnableExtensions
set "ROOT=%~dp0"
set "PKG=%ROOT%package"
set "TP=%ROOT%third_party"
set "WANT_GPU=0"
for %%A in (%*) do if /I "%%~A"=="gpu" set "WANT_GPU=1"

if not exist "%TP%" mkdir "%TP%"

if not exist "%TP%\cmake-4.4.3-windows-x86_64\bin\cmake.exe" (
    echo Extracting CMake...
    powershell -NoProfile -Command "Expand-Archive -Force '%PKG%\cmake\cmake-4.4.3-windows-x86_64.zip' '%TP%'" || goto :fail
)
if not exist "%TP%\opencv\build\OpenCVConfig.cmake" (
    echo Extracting OpenCV 4.14.0...
    "%PKG%\opencv\opencv-4.14.0-windows.exe" -o"%TP%" -y || goto :fail
)
if not exist "%TP%\onnxruntime-win-x64-1.30.0\include" (
    echo Extracting ONNX Runtime 1.30.0 ^(CPU^)...
    powershell -NoProfile -Command "Expand-Archive -Force '%PKG%\onnxruntime\onnxruntime-win-x64-1.30.0.zip' '%TP%'" || goto :fail
)
if "%WANT_GPU%"=="1" if not exist "%TP%\onnxruntime-win-x64-gpu_cuda12-1.30.0\include" (
    echo Extracting ONNX Runtime 1.30.0 ^(CUDA 12^)...
    powershell -NoProfile -Command "Expand-Archive -Force '%PKG%\onnxruntime\onnxruntime-win-x64-gpu_cuda12-1.30.0.zip' '%TP%'" || goto :fail
)
echo Packages ready in "%TP%".
exit /b 0

:fail
echo *** Package extraction failed. ***
exit /b 1
