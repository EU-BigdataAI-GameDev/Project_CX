# [FEAT] 첫 플레이 튜토리얼(코치 마크 6단계)과 적 해킹 범위 1.5배

- 브랜치: `feat/lkj-tutorial-guide` → `develop`

## 개요 (Overview)

처음 만나는 상황마다 한 번만 뜨는 튜토리얼을 추가했습니다. 정지 카드(게임 일시정지)와 행동 안내(슬로모션·비차단)로 이동, 첫 거미, 첫 해킹 단말기, 제미니 첫 대화·규칙, WARDEN 앞 안내를 보여 줍니다. 여동생이 적 해킹 퍼즐을 풀기 전에 맞던 문제를 줄이려고 적 해킹 거리도 1.5배로 늘렸습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `UCXTutorialSubsystem`(`Source/Project_CX/Tutorial/`): 6단계(Move, FirstSpider, FirstTerminal, JeminiHello, JeminiRule, Warden)의 발동·완료·타임아웃 관리. 문구와 시간은 `CXTutorialTypes.cpp`의 `GetSteps()` 한 곳
- `ACoopHUD::DrawTutorial`: 대상 플레이어 칸에 어두운 오버레이, 네온 강조 상자, 한글 카드. 대상이 아닌 칸에는 "파트너가 안내를 읽고 있습니다…" 표시. 키 표기는 플레이어별 장치(키보드/패드)에 맞춤
- 정지 카드는 각자 확인(Enter/Space/패드 A)해야 닫히고, Esc/Start는 단계 건너뛰기. 슬로모션은 0.15배, 최대 6초. 일시정지·슬로모션은 타임아웃이 있어 멈춘 채로 남지 않음
- 봤음 기록은 `UCXGameUserSettings`(`GameUserSettings.ini`)에 저장, 타이틀 NEW GAME이 초기화. 콘솔 `CXTutorialReset`, `CXTutorialShow`, `CXTutorialEnable`, `CXTutorialState`, 실행 인자 `-CXNoTutorial`
- 제미니 예시 문장 칩(키 1~3) → 채팅창에 미리 입력(`OpenChatWithText`)
- ESC 메뉴 `CONTROLS` 조작법 화면(`CXPauseMenuWidget`)
- `ASpiderBot::HackInteractRange` 450 → 675cm(HUD 표시, 대상 탐색, 서버 재검증이 함께 사용). 서버 허용 거리는 이 값 + 슬랙 300cm
- 문서: `Docs/Guide/튜토리얼_가이드.md`, `Docs/Guide/코드_아키텍처.md`, `Docs/TODO.md`·`TODO_Done` 정리
- 헤더 변경 있음(전체 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드 후 Map1 Play(콘솔 `CXTutorialReset`로 기록 초기화)
2. 이동·점프 → 첫 거미 접근 → 해킹 단말기 접근 → 제미니에게 Enter로 인사 → 규칙 카드(1~3) → Map2 끝 WARDEN 앞 순서로 진행
3. 거미에 가까이 가서 여동생으로 적 해킹 시도

**예상 결과:**
각 상황에서 해당 플레이어 칸에 안내가 한 번만 뜨고, 확인하거나 행동하면 닫힘. 정지·슬로모션은 풀리고 다시 뜨지 않음. 적 해킹 시작 거리가 이전보다 넓어짐.

---

## 리뷰 포인트 (Review Focus)

- 정지 카드가 떠 있는 동안 ESC가 일시정지 메뉴가 아니라 건너뛰기로 처리되는 점, 메뉴·패드 연결 끊김 메뉴와의 겹침
- 채팅창이 열려 있거나 녹음 중일 때 정지 카드를 미루는지, 예시 문장 미리 입력 시 포커스와 전송
- 슬로모션 복원이 게임 오버 슬로모션을 덮어쓰지 않는지(값이 0.15 그대로일 때만 복원)
- `HackInteractRange`를 BP·맵 인스턴스가 덮어쓰고 있는지(바이너리라 확인하지 못함)
- PIE·화면 확인은 하지 못함(코드 리뷰와 컴파일만 확인)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
