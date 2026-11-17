@echo off
rem Copy the bundled Jemini server into a packaged game so it starts with the game.
rem Usage: Tools\copy_jemini_to_package.bat <packaged folder that contains Project_CX.exe> [--with-key]
rem   --with-key also copies Jemini\.env (API key). Only for your own machine - never for builds you give to others.
rem Build the server first: Jemini\tools\build_exe.bat
setlocal
set "REPO=%~dp0.."
set "PKG=%~1"
if "%PKG%"=="" (
    echo Usage: %~nx0 ^<packaged folder^> [--with-key]
    exit /b 1
)
if not exist "%PKG%\Project_CX.exe" (
    echo [copy_jemini] Project_CX.exe not found in "%PKG%"
    exit /b 1
)
if not exist "%REPO%\Jemini\dist\jemini-server\jemini-server.exe" (
    echo [copy_jemini] Build the server first: Jemini\tools\build_exe.bat
    exit /b 1
)
rem Includes models\ (live-caption STT model, ~460MB) - build_exe.bat puts it next to the exe. logs\ is not copied.
robocopy "%REPO%\Jemini\dist\jemini-server" "%PKG%\Jemini" /E /XD logs .cache /NFL /NDL /NJH /NJS /NP >nul
if not exist "%PKG%\Jemini\models\faster-whisper-small\model.bin" (
    echo [copy_jemini] WARNING: STT model missing - live captions off. Run Jemini\tools\download_stt_model.py then build_exe.bat again.
)
if "%~2"=="--with-key" (
    copy /Y "%REPO%\Jemini\.env" "%PKG%\Jemini\.env" >nul && echo [copy_jemini] .env copied ^(contains the API key^)
)
echo [copy_jemini] Done: "%PKG%\Jemini\jemini-server.exe"
endlocal
