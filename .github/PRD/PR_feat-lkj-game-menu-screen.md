# [FEAT] 게임 메뉴 화면 5종 + 배경음·효과음 구현

- 브랜치: `feat/lkj-game-menu-screen` → `develop`

## 개요 (Overview)

시작화면 / ESC 메뉴 / 설정 / 게임 클리어 / 게임 오버 화면과 배경음·효과음을 구현했습니다. 위젯은 전부 C++(WBP 없음)이고, 두 플레이어 누구나 키보드·게임패드로 조작할 수 있습니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **메뉴 화면 5종**: 공용 메뉴 프레임워크(`UI/Menu/`) 위에 구현. 새 맵 `Map_Title`
- **설정**: 음량 3종, 화면 분할 상하/좌우, 조작 장치(키보드/패드) 지정 — 진행 세이브와 별도 저장
- **클리어/게임 오버**: 두 버튼 출구 문(`ACXCoopExitDoor`), 사망 시 10초 부활, 둘 다 죽으면 게임 오버. 낙사 판정 포함
- **사운드**: 배경음 2곡(Suno), 효과음 31종(Kenney CC0), 설정 음량 연동
- **입력**: 공용 상호작용 키 F / 패드 X, 해킹 키보드 취소 키 `Escape` → `Backspace`

---

## 스크린샷 / 영상 (Screenshots / Video)

| 시작화면 | 클리어 | 게임 오버 |
|---|---|---|
| ![title](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-game-menu-screen/Docs/Screenshots/Title/title_p2_device_prompt.png?raw=true) | ![clear](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-game-menu-screen/Docs/Screenshots/GameClear/03_clear_screen.png?raw=true) | ![gameover](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-game-menu-screen/Docs/Screenshots/GameOver/02_game_over_brother.png?raw=true) |

나머지: `Docs/Screenshots/`

---

## 테스트 방법 (How to Test)

1. 빌드 후 `Map_Title`에서 Play → 패드 Start(P2 READY) → NEW GAME
2. 인게임에서 ESC → SETTINGS로 음량·화면 분할 변경
3. 콘솔: `CXKillPlayer all`(게임 오버), `CXSpawnExitDoor`(출구 문 → 각자 버튼 F / X)

**예상 결과:**  
메뉴는 화면 전체에 하나만 뜨고 누구나 조작 가능. 타이틀로 갔다가 NEW GAME을 해도 2인 플레이와 ESC 메뉴가 정상 동작.

---

## 리뷰 포인트 (Review Focus)

- **`DefaultEngine.ini`**: 시작 맵이 `Map_Title`로 바뀜, GameInstance / GameUserSettings / ViewportClient 클래스 지정
- **`ALocalCoopGameMode`**: 폰 배정, 클리어·사망·게임 오버 판정이 여기 모여 있음
- **게임플레이 변경**: 총알이 벽에 맞으면 소멸, 해킹 시작이 공용 상호작용 키로 통합

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

- 테스트용 콘솔 명령(`CXKillPlayer`, `CXSpawnExitDoor`)은 일부러 남겨 둠
- 실제 게임패드·원격 클라이언트는 아직 미확인 (PIE + 시뮬레이션 입력으로 테스트)
- 아직 develop에 없는 HUD·해킹 퍼즐 커밋도 이 브랜치에 포함됨
- 상세 기획/구현 결과: `.claude/Plans/`
