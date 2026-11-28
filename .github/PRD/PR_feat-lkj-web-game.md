# [FEAT] 제미니 웹 데모 게임(학교 제출용) + 서버 CORS·웹 토큰

- 브랜치: `feat/lkj-web-game` → `develop`

## 개요 (Overview)

학교 과제("AI 시스템 활용") 제출·발표에서 언리얼 없이 제미니·WARDEN을 보여 주기 위한 **가벼운 웹 게임**을 추가합니다. `Jemini/web/index.html`을 열기만 하면 돌아가고(JS + Canvas, 빌드 없음), AI 판정은 기존 FastAPI 서버를 그대로 호출합니다. 본편(언리얼) 코드는 건드리지 않습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- **웹 게임(`Jemini/web/`):** 단일 맵, 솔로 모드(오빠 조작·여동생 AI 자동 해킹, 화살표 퍼즐 표시), 단말기 1개(해킹 완료 시 증거 E2·E3), 엘리트 거미(E1), 충전 패드, 맵 끝 WARDEN 문
- **제미니 기능 이식(언리얼 C++ → JS):** 명령 9종, 조건부 규칙(`/validate`), 조언·작전 제안, 한 단어 명령, 선제 제안 4종, 평소 코멘트·이벤트 대사, WARDEN 설득·코치. 숫자·문구는 `js/constants.js`에 출처 주석과 함께 모음
- **서버(`Jemini/app`):** `CORSMiddleware` 추가(file://로 연 페이지는 Origin이 null), `WEB_TOKEN` 추가(`API_TOKEN` 또는 `WEB_TOKEN` 중 하나와 맞으면 통과, 둘 다 비면 검사 없음 — 기존 동작 유지, 설정 출력 시 마스킹). 판정 로직은 변경 없음
- **시험:** 서버 pytest 265개(신규 3), JS 순수 로직 23개(`node --test`), 서버 통합 64개(`web/tests/e2e_server.js`, 모의 서버)
- 문서: `Docs/Jemini/웹게임_계획서.md`, `Jemini/web/README.md`(실행법·조작키·수동 시험표)
- 키·토큰은 포함하지 않음(`config.js`의 `token`은 빈 문자열)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. `cd Jemini` → `.venv\Scripts\python.exe -m pytest -q --tb=line`, `node --test web/tests/logic.test.js`
2. `.venv\Scripts\python.exe run_server.py` 후 `Jemini/web/index.html` 더블클릭(키가 없으면 모의 모드 `[MOCK]`, 있으면 `[AI]`)
3. 채팅(Enter)으로 "재장전", "미끼", "체력 30 아래면 회복해 줘" 등 입력, 증거 3개를 모은 뒤 WARDEN 문 앞에서 설득

**예상 결과:**
명령 9종이 효과와 배터리 차감대로 동작하고, 규칙·제안·코멘트가 나오며, 증거 3개로 설득하면 문이 열려 클리어됨.

---

## 리뷰 포인트 (Review Focus)

- 서버 변경은 `main.py`의 CORS·`check_token`, `config.py`의 `web_token` 뿐입니다. CORS는 `allow_origins=["*"]`이며 보호는 토큰이 맡습니다
- Render 반영은 **재배포 필요**(CORS). 웹을 Render에 붙일 때는 서버 환경변수 `WEB_TOKEN`과 `config.js`의 `token`을 같은 값으로
- 실제 키보드 조작·실 Gemini 호출은 PR 작성자가 수동 확인(헤드리스 시험은 못 함). 수치(적 체력·단말기 25초 등)는 웹용 임의값

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
