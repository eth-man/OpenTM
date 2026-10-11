setlocal EnableExtensions EnableDelayedExpansion

set "DEFAULT_VCPKG_ROOT=%USERPROFILE%\vcpkg"

if "%VCPKG_ROOT%"=="" (
    if exist "%DEFAULT_VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=%DEFAULT_VCPKG_ROOT%"
        echo [build.bat] VCPKG_ROOT not set, using %DEFAULT_VCPKG_ROOT%
    ) else (
        echo [build.bat] ERROR: VCPKG_ROOT is not set and %DEFAULT_VCPKG_ROOT% does not contain vcpkg.
        echo [build.bat] Either set VCPKG_ROOT before running, or edit DEFAULT_VCPKG_ROOT in this script.
        exit /b 1
    )
)

cd /d "%~dp0"

if not defined NUMBER_OF_PROCESSORS set "NUMBER_OF_PROCESSORS=8"
set "JOBS=%NUMBER_OF_PROCESSORS%"
echo [build.bat] parallel jobs: %JOBS%

rem ----- parse args -----
set "CONFIG=Debug"
set "PRESET=vs2022"
set "RUN_AFTER=0"
set "DO_CLEAN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="release"  set "CONFIG=Release" & shift & goto parse_args
if /I "%~1"=="debug"    set "CONFIG=Debug"   & shift & goto parse_args
if /I "%~1"=="ninja"    set "PRESET=msvc-debug" & set "CONFIG=Debug" & shift & goto parse_args
if /I "%~1"=="run"      set "RUN_AFTER=1"    & shift & goto parse_args
if /I "%~1"=="clean"    set "DO_CLEAN=1"     & shift & goto parse_args
echo [build.bat] unknown arg: %~1
exit /b 2
:args_done

if "%DO_CLEAN%"=="1" (
    if exist build (
        echo [build.bat] removing build/
        rmdir /s /q build
    )
)

echo [build.bat] cmake --preset %PRESET%
cmake --preset %PRESET% || exit /b 1

if "%PRESET%"=="vs2022" (
    set "BUILD_PRESET=vs2022-debug"
    if /I "%CONFIG%"=="Release" set "BUILD_PRESET=vs2022-release"
    echo [build.bat] cmake --build --preset!BUILD_PRESET! --parallel %JOBS%
    cmake --build --preset!BUILD_PRESET! --parallel %JOBS% || exit /b 1
    set "TEST_PRESET=vs2022-debug"
    set "EXE_DIR=build\vs2022\bin\%CONFIG%"
) else (
    echo [build.bat] cmake --build --preset %PRESET% --parallel %JOBS%
    cmake --build --preset %PRESET% --parallel %JOBS% || exit /b 1
    set "TEST_PRESET=%PRESET%"
    set "EXE_DIR=build\%PRESET%\bin"
)

echo [build.bat] ctest --preset %TEST_PRESET% --parallel %JOBS%
ctest --preset %TEST_PRESET% --parallel %JOBS%

if "%RUN_AFTER%"=="1" (
    set "EXE=%EXE_DIR%\opentm_app.exe"
    if exist "!EXE!" (
        echo [build.bat] launching !EXE!
        start "" "!EXE!"
    ) else (
        echo [build.bat] WARNING: !EXE! not found
        exit /b 3
    )
)

endlocal