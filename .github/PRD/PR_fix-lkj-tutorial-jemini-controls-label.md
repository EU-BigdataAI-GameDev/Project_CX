# [FIX] 패키징 컴파일 오류(PostEditChange) 수정과 조작법 화면 글자 겹침 수정

- 브랜치: `fix/lkj-tutorial-jemini-controls-label` → `develop`

## 개요 (Overview)

PR #45 머지 뒤에 올라간 패키징 컴파일 오류 수정이 develop에 아직 없어 함께 올립니다. 패키징 QA에서 나온 조작법 화면의 글자 겹침도 고쳤습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `CXUIFonts.cpp`: 에디터 전용 `Font->PostEditChange()`를 `#if WITH_EDITOR`로 감싸 게임 타깃(패키징) 컴파일이 통과하게 함
- `CXPauseMenuWidget.cpp`: 조작법 표의 "제미니·WARDEN에게 말하기"를 "제미니·WARDEN 대화"로 줄여 키 표기와 겹치지 않게 함(오빠·여동생 양쪽)
- 패키징 체크리스트·PR #45 문서에 컴파일 오류 대응 메모 반영

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터에서 Package Project로 패키징 (컴파일 오류 없이 완료되는지)
2. ESC 메뉴 → 조작법에서 마지막 줄 확인

**예상 결과:**
패키징이 성공하고, 조작법의 "제미니·WARDEN 대화" 줄이 오른쪽 키 표기와 겹치지 않음.

---

## 리뷰 포인트 (Review Focus)

- 패키징본에서 한글 폰트가 정상 표시되는지(QA에서 패키징 성공 확인됨)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
