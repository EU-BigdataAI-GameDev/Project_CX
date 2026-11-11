# [FEAT] 게임명 CAELUM:UNCHAINED · 아이콘 · UI 폰트 적용

- 브랜치: `feat/lkj-gameTitle-font-icon` → `develop`

## 개요 (Overview)

게임명을 `CAELUM:UNCHAINED`로 확정해 시작화면과 창 제목에 반영하고, 실행 파일·에디터 아이콘을 넣었습니다. HUD·메뉴·해킹·제미니 UI에 폰트 두 종(Oxanium, IBM Plex Mono)을 적용하고, 게임 화면 글자를 전체적으로 3pt씩 키웠습니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **게임명**: 시작화면 제목(Oxanium Bold, 콜론만 마젠타), `DefaultGame.ini`의 `ProjectName`·`ProjectDisplayedTitle`. 저장소·uproject·C++ 모듈·콘텐츠 경로는 `Project_CX` 그대로
- **아이콘**: `Build/Windows/Application.ico`(16~256px 7종), 에디터 썸네일 `Project_CX.png`
- **폰트**: 공용 `UCXUIFonts`로 Slate/Canvas 폰트를 받음
  - Oxanium = 게임 정보(HUD 수치, 메뉴, 알림, 제목) / IBM Plex Mono = 시스템 출력(해킹 퍼즐, 단말기, 제미니 상태·대사·채팅)
  - 두 폰트 모두 한글은 엔진 `DroidSansFallback` 서브 폰트. 에셋 `Content/Project_CX/UI/Fonts/`, 경로는 `[/Script/Project_CX.CXUIFontSettings]`
  - 월드 TextRender(출구 문, 세이브포인트, 숫자 단서)는 엔진 기본 폰트 유지
- **글자 크기**: HUD·메뉴·제미니 채팅창 +3pt(채팅창 폭 760 → 900)
- **문서**: 크레딧에 OFL 폰트 3종 추가, 개요·아키텍처·TODO 갱신, 계획서 `.claude/Plans/UI 폰트·게임명 적용.md`

---

## 스크린샷 / 영상 (Screenshots / Video)

| 시작화면 | HUD + 제미니(한글) | 해킹 패널 |
|---|---|---|
| ![title](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-gameTitle-font-icon/Docs/Screenshots/UIFonts/01_title_screen.png?raw=true) | ![hud](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-gameTitle-font-icon/Docs/Screenshots/UIFonts/big_01_hud_split_jemini.png?raw=true) | ![hack](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/feat/lkj-gameTitle-font-icon/Docs/Screenshots/UIFonts/big_02_hack_circuit.png?raw=true) |

나머지: `Docs/Screenshots/UIFonts/`

---

## 테스트 방법 (How to Test)

1. **헤더가 바뀌었으니 에디터를 닫고 전체 빌드**(Development Editor)
2. `Map_Title`에서 Play. 게임명, 메뉴 폰트 확인
3. 맵에서 HUD(체력·탄약·배터리), 해킹 퍼즐 패널, 제미니 채팅창(한글 입력), 일시정지·게임 오버 화면 확인
4. 창 제목 `CAELUM:UNCHAINED`, 에디터 프로젝트 썸네일 확인

**예상 결과:**
한글이 네모로 깨지지 않고, 글자가 이전보다 커졌으며, 겹치거나 잘리는 곳이 없음.

---

## 리뷰 포인트 (Review Focus)

- `UCXUIFonts`가 한 곳에서 폰트를 받아 쓰므로 이후 UI는 이를 사용
- `CoopHUD.cpp` diff가 큼: 폰트 교체 + 글자 크기 상수화(`HudGrow`) 때문이며 로직 변경은 없음
- 폰트 에셋은 `DefaultGame.ini`의 `DirectoriesToAlwaysCook`에 추가(패키징 누락 방지)
- 폰트 원본(`Art/Fonts`)과 OFL 원문은 저장소에 포함

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

- 미확인: 패키징한 exe에서 아이콘 표시
- 기존 알려진 문제: 제미니 채팅창에서 스페이스 직후 몇 글자가 입력되지 않는 현상(한글 IME 추정, 원인 조사 전)
- 계획·구현 결과: `.claude/Plans/UI 폰트·게임명 적용.md`, 진행도: `Docs/TODO.md`
