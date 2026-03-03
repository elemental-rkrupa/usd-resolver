@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0

if not defined OMNI_USD_FLAVOR set OMNI_USD_FLAVOR=usd
if not defined OMNI_USD_VER set OMNI_USD_VER=24.05
if not defined OMNI_PYTHON_VER set OMNI_PYTHON_VER=3.10

:: Generate the USD Resolver version header and redist deps
:: call "%SCRIPT_DIR%tools\generate.bat" %*
if !errorlevel! neq 0 (goto :end)

:: Build USD Resolver with repo_build
call "%SCRIPT_DIR%repo.bat" build %*
if !errorlevel! neq 0 (goto :end)

:end
    exit /b !errorlevel!