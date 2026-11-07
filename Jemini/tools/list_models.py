"""사용 가능한 Gemini 모델 이름 출력(키 확인용). 실행: python tools/list_models.py"""
import os
import sys
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(Path(__file__).resolve().parent.parent / ".env", override=True)
from google import genai  # noqa: E402

key = os.getenv("GEMINI_API_KEY")
if not key:
    sys.exit("GEMINI_API_KEY가 .env에 없습니다.")
# 클라이언트를 변수에 잡아 둔다(임시 객체면 페이지를 넘기는 도중 닫힌다)
client = genai.Client(api_key=key)
for m in client.models.list():
    actions = getattr(m, "supported_actions", None) or []
    if not actions or "generateContent" in actions:
        print(m.name)
