@echo off
setlocal enabledelayedexpansion

:: build_dependencies.bat
:: Build script for dependencies using NativeDeps/CMakeLists.txt
:: Builds both Debug and Release by default

:: Default values
set "COMPILER=msvc"
set "BUILD_TYPE="
set "VERBOSE="
set "FORCE="
if not defined VKWAVEPKG_ROOT set "VKWAVEPKG_ROOT=%USERPROFILE%\vkwavepkg"

:: Parse arguments
:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--compiler" (
    set "COMPILER=%~2"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--type" (
    set "BUILD_TYPE=%~2"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set "VERBOSE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--force" (
    set "FORCE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="-h" goto :show_help
if /i "%~1"=="--help" goto :show_help
echo ERROR: Unknown option: %~1
goto :show_help
:done_args

:: Validate inputs
call :validate_inputs
if errorlevel 1 exit /b 1

:: Setup directories
call :setup_directories

:: Check existing dependencies
if not defined FORCE (
    call :check_existing_dependencies
    if !errorlevel! equ 2 (
        echo All dependencies already exist. Use --force to rebuild.
        exit /b 0
    )
)

:: Build
if defined BUILD_TYPE (
    call :build_for_type !BUILD_TYPE!
    if errorlevel 1 exit /b 1
) else (
    call :build_for_type Release
    if errorlevel 1 exit /b 1
    call :build_for_type Debug
    if errorlevel 1 exit /b 1
)

:: Verify
call :verify_installation
if errorlevel 1 exit /b 1

:: Clean build dir
call :clean_build_directory

:: Summary
call :show_summary
exit /b 0

:: ========================================
:: Functions
:: ========================================

:show_help
echo build_dependencies.bat - Build dependencies for vkwave
echo.
echo Usage: build_dependencies.bat [options]
echo.
echo Options:
echo   --compiler COMPILER    Compiler to use (msvc^|clang) [default: msvc]
echo   --type BUILD_TYPE      Build type (Debug^|Release) [default: both]
echo   --verbose              Enable verbose output
echo   --force                Force rebuild of dependencies
echo   --help                 Show this help message
echo.
echo Environment Variables:
echo   VKWAVEPKG_ROOT         Root directory for dependencies [default: %%USERPROFILE%%\vkwavepkg]
echo.
echo Examples:
echo   build_dependencies.bat                          Build with defaults (msvc, Debug+Release)
echo   build_dependencies.bat --compiler clang         Build with clang-cl
echo   build_dependencies.bat --type Release           Build Release only
echo   build_dependencies.bat --force --verbose        Force rebuild with verbose output
exit /b 0

:validate_inputs
echo [*] Validating inputs...

if /i not "%COMPILER%"=="msvc" if /i not "%COMPILER%"=="clang" (
    echo ERROR: Invalid compiler: %COMPILER%. Must be 'msvc' or 'clang'
    exit /b 1
)

if defined BUILD_TYPE (
    if /i not "%BUILD_TYPE%"=="Debug" if /i not "%BUILD_TYPE%"=="Release" (
        echo ERROR: Invalid build type: %BUILD_TYPE%. Must be 'Debug' or 'Release'
        exit /b 1
    )
)

if not exist "NativeDeps" (
    echo ERROR: NativeDeps directory not found
    exit /b 1
)

if not exist "NativeDeps\CMakeLists.txt" (
    echo ERROR: NativeDeps\CMakeLists.txt not found
    exit /b 1
)

echo [OK] Input validation completed
exit /b 0

:setup_directories
echo [*] Setting up directories...

set "INSTALL_DIR=%VKWAVEPKG_ROOT%\windows\%COMPILER%\install"
set "BUILD_DIR=%VKWAVEPKG_ROOT%\windows\%COMPILER%\build"

echo     Install directory: %INSTALL_DIR%
echo     Build directory:   %BUILD_DIR%

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [OK] Directories created
exit /b 0

:check_existing_dependencies
echo [*] Checking for existing dependencies...

set "ALL_FOUND=1"

for %%D in (
    "Catch2:lib\cmake\Catch2\Catch2Config.cmake"
    "spdlog:lib\cmake\spdlog\spdlogConfig.cmake"
    "GLFW:lib\cmake\glfw3\glfw3Config.cmake"
    "imgui:lib\cmake\imgui\imguiConfig.cmake"
    "cgltf:include\cgltf.h"
    "stb:include\stb\stb_image.h"
    "toml11:lib\cmake\toml11\toml11Config.cmake"
    "SPIRV-Reflect:lib\cmake\spirv-reflect-static\spirv-reflect-staticConfig.cmake"
) do (
    for /f "tokens=1,2 delims=:" %%A in (%%D) do (
        if exist "%INSTALL_DIR%\%%B" (
            echo [OK] %%A already installed
        ) else (
            set "ALL_FOUND=0"
        )
    )
)

if "!ALL_FOUND!"=="1" exit /b 2

echo [!] Some dependencies missing. Proceeding with build...
exit /b 0

:build_for_type
set "BT=%~1"
echo [*] Building dependencies (%BT%)...

set "TYPE_BUILD_DIR=%BUILD_DIR%\%BT%"

if /i "%COMPILER%"=="msvc" (
    set "CC_COMPILER=cl"
    set "CXX_COMPILER=cl"
) else (
    set "CC_COMPILER=clang-cl"
    set "CXX_COMPILER=clang-cl"
)

set "VERBOSE_FLAG="
if defined VERBOSE set "VERBOSE_FLAG=-DDEPS_VERBOSE=ON"

echo [*] Configuring (%BT%)...
cmake -S NativeDeps -B "%TYPE_BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BT% ^
    "-DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%" ^
    -DCMAKE_C_COMPILER=!CC_COMPILER! ^
    -DCMAKE_CXX_COMPILER=!CXX_COMPILER! ^
    -DBUILD_SHARED_LIBS=ON ^
    -DBUILD_CATCH2=ON ^
    -DBUILD_SPDLOG=ON ^
    -DBUILD_GLFW=ON ^
    -DBUILD_IMGUI=ON ^
    -DBUILD_CGLTF=ON ^
    -DBUILD_STB=ON ^
    -DBUILD_TOML11=ON ^
    -DBUILD_SPIRV_REFLECT=ON ^
    %VERBOSE_FLAG%
if errorlevel 1 (
    echo ERROR: CMake configure failed for %BT%
    exit /b 1
)

echo [*] Building (%BT%)...
if defined VERBOSE (
    cmake --build "%TYPE_BUILD_DIR%" --config %BT% --parallel --verbose
) else (
    cmake --build "%TYPE_BUILD_DIR%" --config %BT% --parallel
)
if errorlevel 1 (
    echo ERROR: CMake build failed for %BT%
    exit /b 1
)

echo [OK] %BT% build completed
exit /b 0

:verify_installation
echo [*] Verifying installation...

set "ALL_GOOD=1"

for %%D in (
    "Catch2:lib\cmake\Catch2\Catch2Config.cmake"
    "spdlog:lib\cmake\spdlog\spdlogConfig.cmake"
    "GLFW:lib\cmake\glfw3\glfw3Config.cmake"
    "imgui:lib\cmake\imgui\imguiConfig.cmake"
    "cgltf:include\cgltf.h"
    "stb:include\stb\stb_image.h"
    "toml11:lib\cmake\toml11\toml11Config.cmake"
    "SPIRV-Reflect:lib\cmake\spirv-reflect-static\spirv-reflect-staticConfig.cmake"
) do (
    for /f "tokens=1,2 delims=:" %%A in (%%D) do (
        if exist "%INSTALL_DIR%\%%B" (
            echo [OK] %%A installation verified
        ) else (
            echo ERROR: %%A installation failed (missing %%B)
            set "ALL_GOOD=0"
        )
    )
)

if "!ALL_GOOD!"=="1" (
    echo [OK] All dependencies verified successfully
    exit /b 0
) else (
    echo ERROR: Some dependencies failed to install
    exit /b 1
)

:clean_build_directory
echo [*] Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo [OK] Build directory removed, keeping only install directory
)
exit /b 0

:show_summary
echo ========================================
echo  Build Summary
echo ========================================
echo  Configuration:
echo    Compiler: %COMPILER%
if defined BUILD_TYPE (
    echo    Build Type: %BUILD_TYPE%
) else (
    echo    Build Types: Release + Debug
)
echo    Install Directory: %INSTALL_DIR%
echo.
echo  Installed Dependencies:
echo    Catch2, spdlog, GLFW, Dear ImGui, cgltf, stb, toml11, SPIRV-Reflect
echo.
echo  Files installed in:
echo    %INSTALL_DIR%\lib         - Libraries
echo    %INSTALL_DIR%\include     - Header files
echo    %INSTALL_DIR%\lib\cmake   - CMake config files
echo.
echo [OK] Build completed successfully!
exit /b 0
