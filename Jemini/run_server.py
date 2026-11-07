"""제미니 서버 실행 진입점 — 게임이 자동으로 띄울 때와 jemini-server.exe(PyInstaller)에서 쓴다.

    python run_server.py [--host 127.0.0.1] [--port 8000]

개발 중에는 `uvicorn app.main:app --port 8000`과 같다.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# 창 없는 exe(PyInstaller --noconsole)나 게임이 숨겨서 띄운 경우 stdout/stderr가 None이라
# uvicorn 로깅이 예외를 내고, 숨은 오류 대화상자에서 멈춘다 → exe 옆 logs/server.log로 보낸다.
if sys.stdout is None or sys.stderr is None:
    _base = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
    (_base / "logs").mkdir(parents=True, exist_ok=True)
    _log = open(_base / "logs" / "server.log", "a", encoding="utf-8", buffering=1)
    if sys.stdout is None:
        sys.stdout = _log
    if sys.stderr is None:
        sys.stderr = _log

import uvicorn  # noqa: E402

from app.main import app  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description="Jemini LLM server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    # 게임이 창 없이 띄우므로 로그는 짧게(요청 기록은 logs/jemini.jsonl)
    uvicorn.run(app, host=args.host, port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
