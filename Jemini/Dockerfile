# 허깅페이스 Docker Space용(2단계). 로컬 실행과 같은 코드, 포트만 7860.
# 키는 Space Settings > Secrets에 GEMINI_API_KEY / API_TOKEN으로 넣는다(.env는 이미지에 넣지 않음).
# 무료 CPU에서는 로컬 실시간 자막(faster-whisper)을 끈다(STT_ENABLED=false). 그래서 가벼운 requirements-space.txt를 쓴다.
FROM python:3.10-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PORT=7860 \
    STT_ENABLED=false

# 허깅페이스 Space는 UID 1000 사용자로 실행된다
RUN useradd -m -u 1000 user
WORKDIR /home/user/app

COPY --chown=user requirements-space.txt .
RUN pip install --no-cache-dir -r requirements-space.txt

COPY --chown=user app ./app
COPY --chown=user prompts ./prompts

# WORKDIR는 root 소유로 만들어지므로, 사용자 전환 전에 logs를 만들고 소유권을 넘긴다(전환 뒤에는 권한이 없어 실패한다)
RUN mkdir -p logs && chown -R user:user /home/user/app

USER user
EXPOSE 7860
CMD ["sh", "-c", "uvicorn app.main:app --host 0.0.0.0 --port ${PORT}"]
