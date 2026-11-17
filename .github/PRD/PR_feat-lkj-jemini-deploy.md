# [FEAT] 제미니 서버 외부 배포(Render) — 배포용 Docker 이미지·워밍업·문서

- 브랜치: `feat/lkj-jemini-deploy` → `develop`

## 개요 (Overview)

제미니(LLM 드론)·WARDEN 서버를 인터넷(Render 무료 웹 서비스)에 올려, 키 없는 게임 배포판도 제미니를 쓰게 했습니다. 허깅페이스 Docker Space가 2026-07부터 무료 계정에서 막혀 Render로 결정했습니다. 서버는 `https://jemini-server.onrender.com`에 배포되어 있고, 에디터 PIE에서 제미니 대화·규칙 등록과 WARDEN 설득이 동작하는 것을 확인했습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `Jemini/Dockerfile`: 배포용 이미지. `requirements-space.txt`(가벼운 의존성), `STT_ENABLED=false`, `logs` 폴더를 `USER user` 전환 전에 만들고 소유권 이전(첫 배포의 `Permission denied` 오류 수정)
- `Jemini/requirements-space.txt`: faster-whisper·테스트 도구 제외
- `Jemini/app/logger.py`, `main.py`: LLM 호출 실패 이유를 호스팅 Logs 화면(stderr)에 남기는 `log_llm_error`(키처럼 보이는 문자열은 가림). 서버 테스트 254개 통과
- `Jemini/tools/deploy_to_repo.ps1`: 서버 파일만 배포용 저장소(`jemini-server`)에 복사·커밋(`.env`·`.venv`·`models` 제외, 비밀 파일 이름 검사). push는 직접
- `CXJeminiServerSubsystem.cpp`: 원격 서버면 게임이 켜질 때 `/health`를 미리 보내 잠든 Render 서버를 깨움(최대 90초, 응답은 쓰지 않음)
- `Config/DefaultGame.ini`: `ServerUrl`을 `https://jemini-server.onrender.com`, `ApiToken`을 배포 서버의 `API_TOKEN`과 같은 값으로 교체(로컬 서버로 시험할 때는 `127.0.0.1:8000`과 빈 토큰으로 되돌림)
- 문서: `Docs/Jemini/서버_배포_가이드.md`(개요·이유·결정 기록·폴더 구조·절차·운영·문제 해결·진행 상황), `Jemini/README.md` 배포 절차, `Docs/TODO.md`
- 헤더 변경 없음(.cpp만)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 열어 `Map1_SoulCity1`에서 PIE, Enter로 제미니에게 말 걸기, 규칙 등록
2. `Map2_SoulCity2`에서 콘솔 `CXGiveEvidence all`, `CXGotoWarden`, Enter로 WARDEN 설득
3. 서버 직접 확인: `https://jemini-server.onrender.com/health`

**예상 결과:**
제미니가 응답하고 규칙이 등록·발동됨. WARDEN이 증거를 근거로 한 설득에만 반응함. 로그에 `JeminiServer: remote warm-up ok`가 보임. 서버가 잠들어 있으면 첫 응답만 30~60초 걸릴 수 있음.

---

## 리뷰 포인트 (Review Focus)

- `ApiToken`이 `DefaultGame.ini`에 들어가므로 저장소를 보는 사람에게 보입니다. 비밀이 아니라 "서버 주소만 아는 외부인"을 막는 용도이고(진짜 비밀인 `GEMINI_API_KEY`는 Render 환경변수에만 있음), 남용이 보이면 Render `API_TOKEN`과 ini를 함께 바꿉니다
- `ServerUrl`이 배포 서버를 가리키므로, 로컬 서버로 시험하려면 ini를 로컬 값으로 되돌려야 함
- Render 무료 인스턴스는 15분 쉬면 잠들어 첫 요청이 느림(워밍업으로 완화)
- `jemini-server`(Private 저장소)와 `Project_CX/Jemini`를 스크립트로 동기화하는 방식. 서버 코드는 `Project_CX/Jemini`에서만 고침

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
