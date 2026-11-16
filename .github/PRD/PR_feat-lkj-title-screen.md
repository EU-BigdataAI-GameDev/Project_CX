# [FEAT] 타이틀 화면 키아트 리디자인

- 브랜치: `feat/lkj-title-screen` → `develop`

## 개요 (Overview)

타이틀 화면에 키아트 배경과 로고 이미지를 적용하고, 켄 번스·로고 연출·메뉴 스타일을 입혔습니다. 시작/이어하기, 설정, 종료 확인, P2 장치 안내 등 기존 동작은 그대로이고 겉모습만 바꿨습니다. `feat/lkj-brother-gun-anim`, `fix/lkj-spider-attack-timing` 위에서 갈라진 브랜치라 그 PR(#37, #38)을 먼저 머지해야 이 PR의 변경만 깔끔하게 보입니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `UCXTitleBackdropWidget`: `Title_Background`를 cover 방식으로 그림, 40초 왕복 켄 번스(줌 1.00→1.06 + 약한 패닝), 왼쪽·하단 그라디언트와 상·우 비네팅, 스캔라인/스윕 라인, 설정·확인창이 열리면 추가 딤. 튜닝 값은 `KenBurns*`, `*ShadeAlpha`, `ScanlineAlpha`, `Sweep*`
- `UCXTitleMenuWidget`: `Title_logo`를 화면 폭 42%로 좌상단에 배치(`LogoWidthFraction`), 0.8초 페이드인 + 상승 + 네온 점등, 3초 주기 숨쉬기 글로우. 로고 아래 "DISTRICT 5" 부제와 메뉴를 한 세로 묶음으로 왼쪽 정렬
- `UCXMenuItemWidget::SetKeyArtStyle`, `UCXMenuWidgetBase::FocusAccentOverride`: 타이틀 메뉴만 마젠타 바 + 그라디언트 강조(포커스 색 마젠타 고정). 설정·확인창 메뉴 모양은 그대로
- 텍스처는 `LoadObject`로 로드하고, 없으면 기존 글자 제목·절차 생성 야경으로 폴백
- 에셋: `Content/Project_CX/Title/`(`Title_Background`, `Title_logo`), 원본 이미지는 `Art/Source/Title/`
- 스크린샷: `Docs/Screenshots/Title/title_keyart_main.png`, `title_keyart_quit_confirm.png`
- 헤더 변경 있음(전체 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

`Docs/Screenshots/Title/title_keyart_main.png`, `title_keyart_quit_confirm.png`

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드 후 `Map_Title` Play
2. 로고 연출, 메뉴 키보드/패드/마우스 이동, 시작·이어하기, 설정, 종료 확인 열기
3. 창 비율을 바꿔 보기(21:9, 4:3)

**예상 결과:**
키아트 배경이 천천히 움직이고 로고가 떠오르며 점등됨. 메뉴 글자와 P1/P2 슬롯이 읽히고, 설정·종료 확인이 열리면 배경이 어두워져 겹쳐도 읽힘. 기존 메뉴 동작은 그대로임.

---

## 리뷰 포인트 (Review Focus)

- 타이틀 메뉴 포커스 색이 "마지막 입력 플레이어 색" 대신 마젠타로 고정된 점(되돌리려면 `CXTitleMenuWidget.cpp`의 `FocusAccentOverride = MenuAccentColor;` 삭제)
- 16:9 외 화면비와 설정 화면, 움직임(켄 번스·숨쉬기)은 PIE로 확인하지 못함(정지 캡처만 확인)
- 텍스처 `.uasset`(약 4MB)은 바이너리라 diff로 확인할 수 없음

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
