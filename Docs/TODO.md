# Project_CX 진행도 (TODO)

- 마지막 갱신: 2026-11-04(레인 앞쪽 깊이 1.1배, Map2 수로 출구 수정, Map1·Map2 세로 1.1배, 세이브 복도 해킹 단말기 제거·상호작용 저장, Map2 단말기 퍼즐 3종, 거미 톱날 회전 축 수정)
- MVP 흐름: 방 1(Map1) → 방 2(Map2) → **세이브 복도** → 마지막 방(보스맵 `Map3_Final`, 팀원 제작) → 출구 문 → 클리어
- 완료하면 `[x]`로 바꾸고, 관련 기획서/커밋을 옆에 적는다.

---

## 필수 (MVP)

### 1. 전투 시스템 — `.claude/Plans/전투 시스템 기획.md`
- [x] 체력 컴포넌트(플레이어 100) + 사망 연결(`Kill(Health)`) + 부활 시 체력 100 — `UCXHealthComponent`
- [x] 플레이어 HP HUD + 피격 무적 1초 / 움찔 / 피격음 / 화면 플래시 — 움찔·플래시는 화면으로 미확인
- [x] 거미로봇 AI(체력 50, 감지 → 추적 → 접촉 중 톱 내려찍기 루프 30 / 1.1초(쿨타임 없음) → 사망) — `ASpiderBot`
- [x] 총알 데미지 10(5발 파괴), 죽은 적 자동 조준 제외, 적 HP 바
- [ ] 거미로봇 애니메이션 — 사망 래그돌, 걷기·공격 — 부분: 래그돌 완료, **공격 완료**(언리얼에서 만든 `AS_SpiderBot_Attack` — 톱 팔 내려찍기 + 톱날 회전 루프), 걷기는 아직 임시안(Idle 재생 속도↑)
- [x] 거미로봇 효과음(보류 해제 — `효과음 적용 기획.md` 2.4) — 기존 에셋 재사용, Windup/Attack_Hit 빈 슬롯, 청취 미확인
- [x] 맵의 기존 거미 배치 → 새 거미로봇으로 교체 — Map1 3마리, Map2 3마리
- [x] 거미 톱날 회전 축 수정 — `AS_SpiderBot_Attack`의 톱날이 팔 방향 축으로 굴러(공중제비) 원판 법선(본 로컬 Z) 회전으로 고침, Idle은 원래 맞음(`전투 시스템 기획.md` "톱날 회전 축 수정", `Tools/Animation/fix_spider_saw_spin.py`, 스크린샷 `Docs/Screenshots/Combat/saw_*`)

### 2. 세이브 복도 — `.claude/Plans/세이브 복도_레벨디자인.md`
- [x] 복도방 레벨 기획 — **22m** 쉼터 "SERVICE CORRIDOR"(적 없음, 평지, 입구 → 세이브 쉼터 → 피트 출입구 3구간, 세이브포인트 1, 따뜻한 세이브 구역 + 차가운 형광등), 배경음 = 시작화면 곡 재사용. v1.0 40m는 너무 길어 단축(도착 화면에서 단말기, 단말기에서 출구가 보이는 길이)
- [x] 복도방 레벨 제작(맵2 → 복도 → 맵3 연결) — `Map_SaveCorridor`(빌더 `Tools/LevelBuild/build_save_corridor.py`, 검증 `validate_save_corridor.py` 전부 통과, 153 액터), Map2 출구 → 복도 도착·세이브·출구 OFFLINE·CONTINUE PIE 재확인(스크린샷 `Docs/Screenshots/SaveCorridor/05`~`08`), 복도 출구 → `Map3_Final`(맵 생기기 전엔 `NEXT AREA OFFLINE`)

### 3. 세이브포인트 — `.claude/Plans/세이브포인트·맵 이동 기획.md`
- [x] 세이브포인트 액터 `ACXSavePoint` + 저장(`CX_Slot0`: 맵, 세이브포인트 ID, 구역명, 시각, 스폰 위치) — 복도에 `SP_Corridor_01` 배치 완료
- [x] 세이브 단말기를 눈에 띄는 에셋으로 — Soul City 발광 튜브 `SM_glowstick02` 빛 기둥 2개 + `SAVE POINT` 간판(대기 주황 → 저장 초록), 해킹 단말기(환풍구 상자)와 다른 실루엣, 자체 충돌 `BodyCollision`(Camera 무시). 에셋 없으면 큐브 단말기 fallback. 후보 비교·탈락 이유는 `세이브포인트·맵 이동 기획.md` 8장 v1.1
- [x] 이어하기(CONTINUE) / 게임 오버 RETRY를 세이브포인트에서 시작하도록 연결(RETRY는 세이브 맵 = 현재 맵일 때만, 아니면 맵 시작)
- [x] 클리어 시 세이브 초기화 확인 — 실제 세이브로 재확인(PIE)
- [x] 세이브를 **상호작용(F / 게임패드 X)**으로 — 닿으면 저장 → 단말기 앞에서 누르면 저장(오빠·여동생 누구나, HUD `[F] SAVE`/`[X] SAVE`), 자동 저장은 `bAutoSaveOnEnter`(기본 꺼짐). 세이브 복도 출구 해킹 단말기·게이트 제거(출구 자유 통과). 복도 재빌드·검증 ALL PASS, PIE `Docs/Screenshots/SaveCorridor/11_*`

### 4. 맵 디테일 보정
- [x] Map1 벽 중간중간 뚫린 부분 메우기 — 11개 추가(시작·끝벽 너머 빈 하늘, S2 계단 뒤 판잣집 밑/틈, S2→S3 먼 판잣집 열 시작 틈, S4 플랫폼 뒷벽 아래). `Tools/LevelBuild/fill_wall_holes.py`(추가 전용, 태그 `GEN_WallFill`), 기록 `Docs/LevelManifests/WallFill_Map1.json`, 전후 캡처 `Docs/Screenshots/MapDetail/`
- [x] Map2 벽 뚫린 부분 메우기 — 2개 추가(시작 끝벽·출구 게이트 너머 빈 하늘). 그 외 레인 뒤 벽은 촘촘해서 손대지 않음. 일부러 남긴 곳: S0→S1 수로 입구 틈(먼 건물 불빛이 보이는 골목), S5 동굴 바위 사이 틈
- [x] 메운 곳 충돌(COL 박스) 확인 — 메운 곳은 전부 레인 뒤 경계벽(`COL_Bound_Back`) 바깥·끝벽 바깥이라 새 COL 불필요. 바닥 구멍은 찾지 못함
- [x] v2: Map1·Map2 벽쪽 건물 "빈 부분 없이"(세이브 복도처럼) — 레인 뒤 건물 열 바로 뒤에 복도와 같은 콘크리트 벽판(`SM_Slums_Wall_02*`) 뒷벽을 이어 세움. Map1 58장(S1·S2·S5 X 575, S3 X 1780 — 타워 유지, S4는 기존 산업 벽), Map2 73장(X 660, S5 바위 뒤 X 1320). 어젯밤 남겨 둔 틈(지붕 위 스카이라인, 판잣집 밑, S3 타워 주변, Map2 수로 입구·S5 바위 사이·S4→S5)도 메움. PIE 원경 숨김 + 발광 판 비교로 비쳐 보이는 틈 0 확인, 새 COL 불필요. 캡처 `Docs/Screenshots/MapDetail/v2_*`, 같은 스크립트(`fill_wall_holes.py`)·manifest

- [x] Map2 수로 출구 수정 + Map1·Map2 세로(Z) 1.1배 — 원인: 판자 발판 P4 밑면이 출구 돌계단 2번째 단 위에 걸려 두 캐릭터 머리가 막힘 → P4 40cm 단축 + 계단 5단(≤40) 재배치. 세로 1.1배는 `Tools/LevelBuild/scale_map2_vertical.py` / `scale_map1_vertical.py`(백업 manifest·undo, 두 번 실행 거부). Map2: 처마 통로 170 유지(분기), 걸어 나오던 구덩이 4곳 +5.5. Map1: 깨진 곳 없음. 스크립트 검사 ALL PASS, PIE(수로 탈출·핵심 점프·해킹 게이트·출구 이동) 확인, 캡처 `Docs/Screenshots/MapDetail/m2scale_*`, `m1scale_*`. `맵2_레벨디자인.md` 12장, `Map1_SoulCity_레벨디자인.md` 8장, `Debug_log.md` #2
- [x] 레인 앞쪽(카메라 쪽) 깊이 1.1배 — "투명벽이 앞으로 못 나오게 막는다" → 앞 경계를 화면 쪽으로. Map1 520 → 572cm(앞 경계 -260 → -312), Map2 270 → 297cm(-120 → -147), 세이브 복도 270 → 297cm(빌더 `DEPTH_FACTOR`). 바닥·발판·계단 COL과 바닥/발판 아트를 같이 늘림, 게이트 COL·레일·막대, 여동생 덕트 천장, 출구·세이브 구역 확장. `Tools/LevelBuild/widen_lane_front.py`(배율 1개, 백업·undo), `add_map{1,2}_hacking.py` 갱신. 스크립트 검사 ALL PASS, PIE 밀기/앞 가장자리 걷기, 캡처 `Docs/Screenshots/MapDetail/lane_*`. 카메라가 캐릭터 X를 따라가서 1.3배도 구도는 같음(원하면 `DEPTH_FACTOR`만 바꿔 undo → 재적용). `Debug_log.md` #6
- [x] Map2·세이브 복도 레인 깊이 1.5배(2026-11-06) — 1.1배(+27cm)가 체감되지 않아 원래 270 → **405cm**(앞 경계 -120 → **-255**, +135). Map2 undo → 1.5배 재적용(늘림 98·이동 11·양 끝 경계 2·게이트 막대 +9·출구 구역 X 270), 복도 `DEPTH_FACTOR` 1.5 재빌드(세이브 구역 X -230~310, 출구 -255~285). Map1은 1.1배 그대로. 검사 ALL PASS, PIE 밀기 Map2 27곳·복도 6곳 → X -220.6 정지(낙하 없음), 게이트 3개 해킹 전 막힘 / `CXSolveHack` 후 열림, Map2 출구 → 복도, 복도 저장·출구. 캡처 `Docs/Screenshots/MapDetail/lane15_*`

### 5. 마지막 방 연결 (팀원 맵 도착 후)
- [ ] `Map3_Final` 받기
- [ ] 보스방 출구에 출구 문(`ACXCoopExitDoor`) 배치
- [ ] 맵 사이 이동(Map1 → Map2 → 복도 → Map3) 연결 — 부분: 출구 `ACXLevelExit`(두 명 생존·도착 시 이동) 구현, **Map1 → Map2 연결·확인**, **Map2 → 세이브 복도 연결·확인**(PIE), 복도 출구(→ `Map3_Final`) 배치 완료(맵 없으면 `NEXT AREA OFFLINE`). 남은 것: Map3 도착 확인

---

## 남은 자잘한 것

- [ ] 게임 아이콘 추가(실행 파일 / 창 아이콘)
- [ ] Map2 배경음 제작(Suno) 및 적용 — `Docs/배경음_제작기록.md`, `ALocalCoopGameMode::MapMusic`
- [ ] 해킹 HUD 안내 `[B] ABORT` → 키보드면 `[Backspace]` 표시
- [x] 여동생 적 해킹 → 거미로봇 **3초 그로기** (`해킹 시스템 기획.md` 10장 — 기획서 5초 → 3초) — 적 퍼즐은 **화살표 1라운드 5칸·4초**(처음 구현한 "주파수 동조"에서 교체 — 아래 줄), 쿨타임 3초, 그로기 연출(톱 팔 떨굼·경련·정전 깜빡임·청록/자홍 불빛·스파크·재부팅음), 두 화면 `SYSTEM DOWN` 마커. 실제 패드 손맛·네트워크 PIE 미확인
- [x] 해킹 퍼즐 배정 변경 (`해킹 시스템 기획.md` 0-1·10.2·12장) — 적 해킹 = 화살표 빠른 입력(1x5, 4초, 틀리면 같은 배열 처음부터, 시간 초과 실패), 주파수 동조 → 단말기 퍼즐 `FrequencyLock`(숫자 그대로, 제한 시간 없음), 단말기 화살표 폐기(`ArrowSequence` 숨김 + 남아 있으면 경고 후 FrequencyLock). Map1 `CORE_MAINFRAME` = 주파수 동조. 퍼즐 규칙은 `CXHackPuzzles.h` 구조체로 분리. PIE 검증 스크린샷 `Docs/Screenshots/Hacking/swap_*`. 실제 패드 손맛·네트워크 PIE 미확인
- [x] Map2 단말기 퍼즐 3종 배치(`해킹 시스템 기획.md` 13장) — 비밀번호 `SHUTTER_CTRL-5`(S2 합류 → S3 문 게이트, 단서 4개 오빠 동선), 주파수 동조 `MARKET_RELAY`(S4 입구 → 낮은 마당 게이트), 메모리 패턴 `CAVE_LIFT_ECHO`(S5 바위 A → 구덩이 게이트, 오빠 9m 중계). `Tools/LevelBuild/add_map2_hacking.py`(태그 `GEN_Map2Hack`, 추가만) 검사 ALL PASS, PIE 3종 해킹 → 게이트 해제 확인 `Docs/Screenshots/Hacking/map2_*`. 실제 패드 손맛·네트워크 PIE 미확인
- [x] 협동 해킹 퍼즐 D **메모리 패턴(Ghost Echo)** (`해킹 시스템 기획.md` 11장) — 3x3 숫자판(1~9), 점멸 패턴은 **오빠 화면에만**(PATTERN RELAY 패널), 여동생은 D-pad 커서 + F/X로 입력, 라운드 3→4→5(사이먼식), 오빠가 단말기 9m 안에 살아 있어야 재생·입력, 오답은 같은 패턴 재생. 세이브 복도 출구 앞 단말기 + 레이저 게이트 배치(검증 ALL PASS, 스크린샷 `Docs/Screenshots/Hacking/mem_*`) → 2026-11-04 복도에서는 제거, Map2 S5로 옮김. 실제 패드 손맛·네트워크 PIE 미확인
- [ ] 실제 게임패드·마우스로 전체 메뉴/플레이 테스트(지금까지는 시뮬레이션 입력)
- [ ] 패키징 빌드 테스트(`Map_Title`로 시작하는지)
- [ ] 효과음 밸런스·발소리 타이밍 귀로 확인 후 조정
- [ ] PR 템플릿이 `main`에 없음 → develop을 main에 머지하거나 `.github`만 올리기

---

## 완료

- [x] 맵1·맵2 레벨 디자인, 조명, 소품 충돌
- [x] 오빠 자동 조준 사격(`자동 조준 사격 구현.md`), 여동생 해킹 퍼즐(`해킹 시스템 기획.md`)
- [x] 인게임 HUD(`HUD 기획.md`)
- [x] 메뉴 화면 5종 — 시작 / ESC / 설정 / 클리어 / 게임 오버 + 부활 흐름 (PR #11)
- [x] 배경음 2곡(시작화면, Map1), 효과음 31종
- [x] 빌드 오류 해결(git 한글 경로 → `Tools/setup_git.bat`), MetaHuman 플러그인 제거
