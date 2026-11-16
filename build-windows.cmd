@echo off
setlocal
cd /d "%~dp0"
echo Glimpse Windows build
echo Building and saving output to "%~dp0build.log"...
echo This window will stay open when the build finishes.
call :build > "%~dp0build.log" 2>&1
set "GLIMPSE_RESULT=%ERRORLEVEL%"
type "%~dp0build.log"
echo.
if "%GLIMPSE_RESULT%"=="0" goto success
echo BUILD FAILED. No new executable is being claimed as ready.
echo Please send the build.log file beside this script for diagnosis.
goto finish
:success
echo BUILD SUCCEEDED: portable\Glimpse.exe
echo Portable package: Glimpse-Windows-x64.zip
:finish
echo.
echo Full log: "%~dp0build.log"
pause
exit /b %GLIMPSE_RESULT%

:build
echo Glimpse build started: %DATE% %TIME%
ver
if not exist "%~dp0CMakeLists.txt" (
  echo ERROR: Extract the source ZIP first. Put this script beside CMakeLists.txt.
  exit /b 1
)
set "GLIMPSE_CMAKE="
set "GLIMPSE_CTEST="
set "GLIMPSE_VS="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined GLIMPSE_CMAKE set "GLIMPSE_CMAKE=%%I"
if defined GLIMPSE_CMAKE goto foundcmake
set "GLIMPSE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%GLIMPSE_VSWHERE%" goto missingtools
for /f "usebackq delims=" %%I in (`"%GLIMPSE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GLIMPSE_VS=%%I"
if not defined GLIMPSE_VS goto missingtools
set "GLIMPSE_CMAKE=%GLIMPSE_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%GLIMPSE_CMAKE%" goto missingtools
:foundcmake
for %%I in ("%GLIMPSE_CMAKE%") do set "GLIMPSE_CTEST=%%~dpIctest.exe"
if not exist "%GLIMPSE_CTEST%" (
  echo ERROR: ctest.exe is missing beside CMake. Repair the CMake installation.
  exit /b 1
)
echo CMake: "%GLIMPSE_CMAKE%"
"%GLIMPSE_CMAKE%" --version
rem A separate directory avoids the old generator and moved-folder caches.
"%GLIMPSE_CMAKE%" -S "%~dp0." -B "%~dp0build-diagnostic" -A x64 -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1
rem Single-job output keeps compiler errors readable.
"%GLIMPSE_CMAKE%" --build "%~dp0build-diagnostic" --config Release --parallel 1
if errorlevel 1 exit /b 1
"%GLIMPSE_CTEST%" --test-dir "%~dp0build-diagnostic" -C Release --output-on-failure
if errorlevel 1 exit /b 1
"%GLIMPSE_CMAKE%" --install "%~dp0build-diagnostic" --config Release --prefix "%~dp0portable"
if errorlevel 1 exit /b 1
if not exist "%~dp0portable\Glimpse.exe" (
  echo ERROR: Build did not produce portable\Glimpse.exe.
  exit /b 1
)
powershell.exe -NoProfile -Command "$ErrorActionPreference='Stop'; Compress-Archive -Path 'portable\*' -DestinationPath 'Glimpse-Windows-x64.zip' -Force"
if errorlevel 1 exit /b 1
exit /b 0
:missingtools
echo ERROR: CMake or the Visual Studio C++ toolchain was not found.
echo In Visual Studio Installer, select Desktop development with C++.
echo Include C++ CMake tools for Windows and a Windows 10 or Windows 11 SDK.
echo Then run this script again. No changes were made to your installations.
exit /b 1
