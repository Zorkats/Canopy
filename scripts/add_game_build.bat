@echo off
rem Adds a game update to src\common\game_builds.txt, so Canopy treats it as
rem a checked build.
rem
rem   scripts\add_game_build.bat [village.exe] ["label"] [previous village.exe] [--none Name]...
rem
rem Without arguments it asks for the exe and the label, like
rem "Ver. 1.xx (Steam build 12345678)". The previous exe is a build
rem game_builds.txt already lists, such as "_work\bin\village 1.30.exe"; with
rem it the review also lists functions whose length changed. Give --none Name
rem for a site you checked the new build no longer has.
rem
rem It searches the exe, prints what to review, asks before it writes, then
rem copies the exe to _work\bin for the tests, rebuilds, checks the new
rem section against a fresh search and runs the tests. Needs the build
rem folder configured, and Visual Studio 2022 or its Build Tools.
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CANOPY=%ROOT%\build\src\CanopyTools\canopy.exe"
set "BUILDS=%ROOT%\src\common\game_builds.txt"
set "SAMPLES=%ROOT%\_work\bin"

set "EXE="
set "LABEL="
set "PREVIOUS="
set "EXTRA="
:arguments
if "%~1"=="" goto parsed
if /i "%~1"=="--none" goto none
if not defined EXE (
    set "EXE=%~1"
) else if not defined LABEL (
    set "LABEL=%~1"
) else if not defined PREVIOUS (
    set "PREVIOUS=%~1"
)
shift
goto arguments
:none
set EXTRA=%EXTRA% --none "%~2"
shift
shift
goto arguments
:parsed

if not defined EXE set /p "EXE=Path to the new village.exe: "
if not defined EXE goto noExe
set "EXE=%EXE:"=%"
if not exist "%EXE%" goto noExe
if not defined LABEL set /p "LABEL=Label, like Ver. 1.xx (Steam build 12345678): "
if not defined LABEL goto noLabel
if defined PREVIOUS set EXTRA=--previous "%PREVIOUS%" %EXTRA%
if not exist "%CANOPY%" goto noCanopy

echo.
"%CANOPY%" addresses "%EXE%" --builds "%BUILDS%" --add-build "%LABEL%" %EXTRA%
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="3" exit /b 0
if not "%RESULT%"=="0" exit /b %RESULT%

echo.
choice /c YN /m "Add this build to game_builds.txt"
if errorlevel 2 exit /b 1
"%CANOPY%" addresses "%EXE%" --builds "%BUILDS%" --add-build "%LABEL%" %EXTRA% --write --sample "%SAMPLES%"
if errorlevel 1 exit /b %ERRORLEVEL%

rem The tables are built into Canopy, so it is rebuilt to use the new one.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto noVisualStudio
set "VS="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%I"
if not defined VS goto noVisualStudio
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
echo.
echo Rebuilding...
cmake --build "%ROOT%\build"
if errorlevel 1 exit /b 2
echo.
"%CANOPY%" addresses "%EXE%" --check
if errorlevel 1 exit /b 1
pushd "%ROOT%\build"
ctest --output-on-failure
set "RESULT=%ERRORLEVEL%"
popd
if not "%RESULT%"=="0" exit /b 1
echo.
echo Added the build. Review the diff of src\common\game_builds.txt before you commit it.
exit /b 0

:noExe
echo The new village.exe was not found.
exit /b 2
:noLabel
echo The build needs a label.
exit /b 2
:noCanopy
echo canopy.exe is missing from build\src\CanopyTools. Build the project first: cmake --build build
exit /b 2
:noVisualStudio
echo Visual Studio was not found, so rebuild and test by hand: cmake --build build, then ctest in build.
exit /b 0
