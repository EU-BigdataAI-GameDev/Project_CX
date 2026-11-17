# [FEAT] 전투 시스템·해킹 퍼즐 개편·세이브 복도 + 맵 보정

- 브랜치: `fix/lkj-frequence-puzzle` → `develop`

## 개요 (Overview)

MVP 남은 기능(전투, 세이브포인트, 세이브 복도)을 구현하고, 해킹 퍼즐을 적/단말기 용도로 나눠 개편했습니다. 플레이 테스트 피드백으로 주파수 퍼즐 난이도, 연사 속도, Map2 수로 탈출 불가, 레인 깊이를 수정했습니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **전투**: 체력 100 / 거미로봇(체력 50, 총알 10 × 5발, 톱날 내려찍기 30), 피격 무적·움찔, HP HUD, 부활·게임 오버 연결
- **해킹**: 적 = 화살표(5개 / 4초 → 거미 그로기 3초), 단말기 = 비밀번호·주파수 동조·메모리 패턴(오빠만 패턴을 보는 협동)
- **흐름**: 세이브포인트(F/X로 저장, CONTINUE/RETRY 연결), 맵 출구(두 명 도착 시 이동), 세이브 복도 `Map_SaveCorridor`
- **맵**: Map1·Map2 거미·단말기 배치, 뒤쪽 벽 채움, 세로 1.1배, W/S 깊이 확장(Map1 1.1배, Map2·복도 1.5배), Map2 수로 출구 수정
- **조정**: 주파수 퍼즐 난이도 완화·HUD 최적화, 연사 0.3초, 적 해킹 범위 450cm, MetaHuman 플러그인 제거

---

## 스크린샷 / 영상 (Screenshots / Video)

| 거미 전투 | 메모리 패턴(협동) | 세이브 복도 |
|---|---|---|
| ![combat](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-frequence-puzzle/Docs/Screenshots/Combat/06_spider_chop_impact.png?raw=true) | ![memory](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-frequence-puzzle/Docs/Screenshots/Hacking/mem_02_brother_flash_sister_grid.png?raw=true) | ![corridor](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-frequence-puzzle/Docs/Screenshots/SaveCorridor/11_interact_pressed_progress_saved.png?raw=true) |

나머지: `Docs/Screenshots/`

---

## 테스트 방법 (How to Test)

1. 클론/풀 후 `Tools/setup_git.bat` 한 번 실행 → 빌드 → `Map_Title`에서 Play (2P 패드 필요)
2. Map1 → Map2 → 세이브 복도 순서로 진행: 거미 처치, 단말기 해킹, 출구 이동, 복도에서 F/X 저장
3. 콘솔: `CXSpawnSpider`, `CXSolveHack`(해킹 강제 성공), `CXKillPlayer all`, `CXGotoNextMap`

**예상 결과:**  
거미는 5발에 래그돌로 쓰러지고, 해킹 게이트는 퍼즐 성공 후 열리며, 복도 저장 후 타이틀 CONTINUE로 저장 위치에서 시작. 복도 출구는 `Map3_Final`이 없어 `NEXT AREA OFFLINE` 표시.

---

## 리뷰 포인트 (Review Focus)

- **맵 대량 변경**: Map1/Map2는 스크립트(`Tools/LevelBuild/scale_*_vertical.py`, `widen_lane_front.py`)로 수정 — 원본 트랜스폼 백업과 되돌리기 포함(`Docs/LevelManifests/`)
- **새 C++**: `UCXHealthComponent`, `ASpiderBot`, `CXHackPuzzles`, `ACXSavePoint`, `ACXLevelExit`
- **설정**: 패키징 맵 목록·항상 쿠킹 폴더, 에디터 시작 맵 `Map1_SoulCity1`

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

- `feat/lkj-battle-system` 커밋도 포함되어 있음(develop 미머지 상태) — 이 PR 하나로 함께 머지
- 테스트용 콘솔 명령은 일부러 남겨 둠
- 미확인: 네트워크 클라이언트, 거미 걷기 애니메이션(임시)
- 버그·수정 기록: `Docs/Debug_log.md`, 진행도: `Docs/TODO.md`
