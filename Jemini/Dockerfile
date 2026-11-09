# 허깅페이스 Docker Space용(2단계). 로컬 실행과 같은 코드, 포트만 7860.
# 키는 Space Settings > Secrets에 GEMINI_API_KEY / API_TOKEN으로 넣는다(.env는 이미지에 넣지 않음).
FROM python:3.10-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PORT=7860

# 허깅페이스 Space는 UID 1000 사용자로 실행된다
RUN useradd -m -u 1000 user
WORKDIR /home/user/app

COPY --chown=user requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY --chown=user app ./app
COPY --chown=user prompts ./prompts

USER user
RUN mkdir -p logs
EXPOSE 7860
CMD ["sh", "-c", "uvicorn app.main:app --host 0.0.0.0 --port ${PORT}"]
