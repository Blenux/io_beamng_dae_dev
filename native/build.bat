@echo off
REM Build cdae_native as a Python wheel for Windows.
REM Requires: Python 3.11/3.13 installed, Visual Studio Build Tools (C++ workload).
REM
REM Usage:
REM   build.bat                  # Build for all installed Python versions
REM   build.bat 3.11             # Build for specific version only
REM   build.bat 3.11 3.13        # Build for specific versions

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM Determine which Python versions to build for
if "%~1"=="" (
    set PY_VERSIONS=3.11 3.13
) else (
    set PY_VERSIONS=%*
)

REM Iterate using goto-based loop (goto inside for breaks batch parsing)
set REMAINING=%PY_VERSIONS%
:loop_start
if "%REMAINING%"=="" goto :loop_end

REM Extract first token
for /f "tokens=1,*" %%A in ("%REMAINING%") do (
    set CURRENT=%%A
    set REMAINING=%%B
)

call :build_version %CURRENT%
goto :loop_start

:loop_end

echo.
echo ============================================
echo Build complete. Wheel(s) in ..\io_beamng_dae\wheels\:
if exist ..\io_beamng_dae\wheels\cdae_native-*.whl (
    dir ..\io_beamng_dae\wheels\cdae_native-*.whl
) else (
    echo No wheels found in ..\io_beamng_dae\wheels\
)

REM Update blender_manifest.toml with built wheel(s) and platform(s)
echo.
echo Updating blender_manifest.toml...
python "%SCRIPT_DIR%\update_manifest.py"

goto :eof

REM === Subroutine: build for a single Python version ===
:build_version
set PYVER=%~1

echo.
echo ============================================
echo Building for Python %PYVER%
echo ============================================

REM Find Python executable for this version
where py -%PYVER% >nul 2>nul
if !errorlevel! equ 0 (
    set PY_CMD=py -%PYVER%
) else (
    where python%PYVER% >nul 2>nul
    if !errorlevel! equ 0 (
        set PY_CMD=python%PYVER%
    ) else (
        echo Error: Python %PYVER% not found. Install it or use: build.bat %PYVER%
        echo Skipping...
        goto :eof
    )
)

REM Create venv
echo Creating venv for Python %PYVER%...
!PY_CMD! -m venv .build_venv_%PYVER%
if !errorlevel! neq 0 (
    echo Error: Failed to create venv for Python %PYVER%
    goto :eof
)

call .build_venv_%PYVER%\Scripts\activate.bat

echo Installing build dependencies...
python -m pip install --upgrade pip build pybind11 cmake scikit-build-core

echo Building wheel...
python -m build --wheel --outdir "%SCRIPT_DIR%\..\io_beamng_dae\wheels"

if !errorlevel! equ 0 (
    echo Build successful for Python %PYVER%
) else (
    echo Build FAILED for Python %PYVER%
)

call deactivate
rmdir /s /q .build_venv_%PYVER%
goto :eof
