@echo off
rem Bundle the Jemini server into jemini-server.exe (one folder) so a packaged game can start it without Python.
rem Output: Jemini\dist\jemini-server\jemini-server.exe (+ _internal\, models\ = live-caption STT model)
rem Then copy it into the packaged game: Tools\copy_jemini_to_package.bat <package folder>
setlocal
cd /d "%~dp0.."

if not exist .venv\Scripts\python.exe (
    echo [build_exe] .venv not found. Create it first ^(see Jemini\README.md^).
    exit /b 1
)

.venv\Scripts\python -m pip install -q pyinstaller -r requirements.txt || exit /b 1

rem Live captions (POST /stt/partial): download the faster-whisper model once (models\faster-whisper-small, ~460MB)
.venv\Scripts\python tools\download_stt_model.py || echo [build_exe] WARNING: STT model download failed - live captions will be off until models\ exists

rem faster_whisper/ctranslate2/tokenizers/av carry native DLLs and data files -> collect them whole.
rem onnxruntime is only used by the VAD filter, which the server does not use -> excluded (smaller build).
rem pandas/pyarrow/PIL come in through optional imports (demo/ packages in the same venv) -> excluded (~110MB).
.venv\Scripts\python -m PyInstaller --noconfirm --clean --onedir --noconsole ^
    --name jemini-server ^
    --add-data "prompts;prompts" ^
    --collect-submodules uvicorn ^
    --collect-data google.genai ^
    --collect-all faster_whisper ^
    --collect-all ctranslate2 ^
    --collect-all tokenizers ^
    --collect-all av ^
    --collect-submodules huggingface_hub ^
    --exclude-module onnxruntime ^
    --exclude-module sympy ^
    --exclude-module pandas ^
    --exclude-module pyarrow ^
    --exclude-module PIL ^
    --exclude-module streamlit ^
    --exclude-module altair ^
    run_server.py || exit /b 1

rem The model ships next to the exe (STT_MODEL_DIR default = <exe folder>\models)
if exist models\faster-whisper-small\model.bin (
    robocopy models dist\jemini-server\models /E /XD .cache /NFL /NDL /NJH /NJS /NP >nul
    echo [build_exe] STT model copied to dist\jemini-server\models
)

echo.
echo [build_exe] Done: %cd%\dist\jemini-server\jemini-server.exe
echo [build_exe] Put .env next to the exe. It contains the API key - never ship it in a build for other people.
endlocal
