@echo off
chcp 65001 > nul

set BUILD_DIR=build
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

if "%2"=="--clean" (
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
)

if not exist %BUILD_DIR% (
    mkdir %BUILD_DIR%
    cmake -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=%CONFIG%
)

cmake --build %BUILD_DIR% --config %CONFIG% -- -j%NUMBER_OF_PROCESSORS%

echo.
echo Build complete. Output: %BUILD_DIR%/Gw2ItemSearch.dll
