# [FEAT] 제미니 AI 드론 — LLM 서버 + 드론 기능 9종

- 브랜치: `feat/lkj-jemini-AIbot` → `develop`

## 개요 (Overview)

플레이어가 채팅(Enter)이나 음성(패드 Y 누르고 말하기)으로 부탁하면 지원 드론 "제미니"가 대사를 말하고 실제 게임 효과를 실행합니다. 판단은 Gemini 함수 호출(FastAPI 서버), 실행 여부·배터리는 서버 검증기가 결정하고 UE(리슨 서버)가 효과를 적용합니다. 학교 과제(LLM 활용) 겸 포트폴리오 기능입니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **서버 `Jemini/`**: FastAPI `/command`·`/command/voice`, Gemini 강제 함수 호출(함수 10개 = 기능 9 + Chat), 검증기(조건·배터리·거절 사유), 429 시 예비 모델 → 모두 한도 초과면 mock 키워드 판별(`[MOCK]` 표시), 키 없으면 mock 모드, 테스트 96개, PyInstaller exe
- **드론 기능**: 재장전 5 / 단말기 해킹 20 / 적 해킹 20 / 펄스 공격 50 / 공격력 x2 10 / 회복 30 / 미끼 15 / 빠른 재부팅 30 / 보호막 25(배터리, 실제 실행 시에만 차감)
- **UE**: `ACXJeminiDrone`(복제, 비행·연출·표정), `CXJeminiFx`, 채팅창(Slate)·음성 녹음(16kHz WAV), 쓰러진 플레이어도 말하기, HUD(배터리·BUSY·LOW BATTERY·대사 말풍선)
- **서버 자동 실행**: `UCXJeminiServerSubsystem` — 게임을 켜면 서버가 켜지고 끄면 꺼짐(직접 띄운 서버가 있으면 그대로 사용)
- **연동**: 오빠 자동 재장전 제거(제미니에게 부탁), 해킹 대리 진행, 체력 회복·보호막, 거미 미끼 추적, 배터리 세이브·맵 이동 +30 충전
- **에셋**: `SM_SupportDrone`, 표정 아틀라스·스크린 머티리얼 / **맵**: `Map1_SoulCity1` 거미 추가·위치 조정(손 편집)

---

## 스크린샷 / 영상 (Screenshots / Video)

| 표정 시트 | 기쁨 | 동료 쓰러짐(화남) |
|---|---|---|
| ![emotions](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-jemini-AIbot/Docs/Screenshots/Drone/emotion_sheet.png?raw=true) | ![happy](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-jemini-AIbot/Docs/Screenshots/Drone/pie_happy.png?raw=true) | ![angry](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-jemini-AIbot/Docs/Screenshots/Drone/pie_angry_partner_down.png?raw=true) |

---

## 테스트 방법 (How to Test)

1. 서버 준비(한 번): `cd Jemini` → `python -m venv .venv` → `.venv\Scripts\pip install -r requirements.txt`. 실제 Gemini를 쓰려면 `Jemini/.env`에 `GEMINI_API_KEY`(없으면 mock 모드로 동작)
2. 빌드 → Play. 서버는 자동으로 켜짐
3. Enter → "재장전해줘", "힐 좀 해줘", "적 공격해" 등 입력 / 패드 Y 누르고 말하기
4. 콘솔: `CXJemini <문장>`, `CXJeminiSister <문장>`, `CXJeminiCmd <기능> [brother|sister]`(LLM 없이 실행), `CXJeminiBattery <0~100>`, `CXJeminiSelfTest`(9종 순서 실행)
5. 서버 테스트: `cd Jemini` → `.venv\Scripts\python -m pytest -q`

**예상 결과:**
드론이 대사를 말하며 대상에게 날아가 효과 실행, 배터리 차감. 조건이 안 맞으면 거절 대사(차감 없음). 서버가 없으면 게임은 그대로 돌고 제미니만 "신호가 끊겼어요".

---

## 리뷰 포인트 (Review Focus)

- **API 키**: `Jemini/.env`는 gitignore — 커밋에 없음. 키를 포함한 패키징은 내 PC용만(`Docs/Guide/빌드_실행.md`)
- **기존 코드 변경**: `BrotherCombatComponent`(자동 재장전 제거, 공격력 배율), `SisterHackComponent`(대리 해킹), `CXHealthComponent`(Heal·Shield), `SpiderBot`(미끼), `LocalCoopGameMode`(드론 스폰·충전·콘솔)
- **설정**: `DefaultGame.ini` `[/Script/Project_CX.CXJeminiDrone] ServerUrl`은 따옴표 필수(`//` 뒤가 잘림)
- 명세: `Docs/Jemini/02_요구사항_명세서.md`, 기획: `.claude/Plans/제미니 기능 기획.md`(+ `Jemini_plans/`)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [ ] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인

### 리뷰어 (Reviewer)
- [ ] 변경 사항 이해 및 의도 파악
- [ ] 코드 로직 검토
- [ ] 직접 빌드하여 테스트
- [ ] 네이밍 / 컨벤션 준수 확인

---

## 추가 메모 (Additional Notes)

- 무료 Gemini는 하루 요청 한도가 있음 → 예비 모델, 그다음 mock으로 넘어감(HUD `[MOCK]`). 음성은 mock 불가
- 테스트용 콘솔 명령은 일부러 남겨 둠
- 미확인: 원격 클라이언트 경로, HF Space 배포
- 버그·수정 기록: `Docs/Debug_log.md`, 진행도: `Docs/TODO.md`
