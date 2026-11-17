# [FIX] 해킹 HUD 안내를 여동생 입력 장치(키보드/패드)에 맞게 표시

- 브랜치: `fix/lkj-hack-hud-keyboard-labels` → `develop`

## 개요 (Overview)

여동생이 키보드를 쓸 때도 해킹 화면 안내가 `[B] ABORT`, `D-PAD`, `[X]`처럼 패드 표기로만 나오던 문제를 고쳤습니다. 이제 여동생의 입력 장치에 따라 안내가 바뀝니다. `feat/lkj-opening-cutscene`(#42) 위에서 갈라진 브랜치라 그 PR을 먼저 머지해야 이 PR의 변경만 깔끔하게 보입니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `CoopHUD.cpp`: `UCXGameUserSettings`의 여동생 슬롯 장치를 읽어 해킹 안내를 바꾸는 도우미(`DpadLabel`, `AbortKey`, `KeyHint`) 추가
  - 키보드: `[BACKSPACE] ABORT`, `ARROWS`, `[F]` / 패드: `[B] ABORT`, `D-PAD`, `[X]`
  - 적 해킹, 주파수, 회로 연결, 메모리 패턴 단말기의 하단 안내와 `HACK`·`BREACH` 표시에 적용
- 헤더 변경 없음(`HackKeyHint`는 패드 기본값으로 유지)
- 문서: `Docs/TODO.md`에서 빈 섹션(퀄리티 향상, 남은 자잘한 것)을 정리하고 완료 항목을 `TODO_Done`으로 이동

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 빌드 후 ESC 메뉴 → 입력 장치에서 여동생을 키보드로 바꾸기(오빠는 게임패드)
2. 적 해킹, 단말기 해킹 화면 열기
3. 여동생을 다시 게임패드로 바꿔 같은 화면 확인

**예상 결과:**
키보드일 때 `[BACKSPACE] ABORT`, `ARROWS`, `[F]`가, 패드일 때 `[B] ABORT`, `D-PAD`, `[X]`가 표시됨.

---

## 리뷰 포인트 (Review Focus)

- 여동생 슬롯 인덱스(1)로 장치를 읽는 부분과, 키보드 표기(`F`, 방향키, `Backspace`)가 `SisterHackComponent`의 실제 입력과 맞는지
- 화면 확인은 하지 못함(컴파일 성공만 확인)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
