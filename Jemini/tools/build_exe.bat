@echo off
rem Bundle the Jemini server into jemini-server.exe (one folder) so a packaged game can start it without Python.
rem Output: Jemini\dist\jemini-server\jemini-server.exe (+ _internal\)
rem Then copy it into the packaged game: Tools\copy_jemini_to_package.bat <package folder>
setlocal
cd /d "%~dp0.."

if not exist .venv\Scripts\python.exe (
    echo [build_exe] .venv not found. Create it first ^(see Jemini\README.md^).
    exit /b 1
)

.venv\Scripts\python -m pip install -q pyinstaller || exit /b 1
.venv\Scripts\python -m PyInstaller --noconfirm --clean --onedir --noconsole ^
    --name jemini-server ^
    --add-data "prompts;prompts" ^
    --collect-submodules uvicorn ^
    --collect-data google.genai ^
    run_server.py || exit /b 1

echo.
echo [build_exe] Done: %cd%\dist\jemini-server\jemini-server.exe
echo [build_exe] Put .env next to the exe. It contains the API key - never ship it in a build for other people.
endlocal
