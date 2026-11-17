# [DOCS] 업로드 가이드에 PowerShell 큰따옴표·출력 주의점 추가

- 브랜치: `docs/lkj-guide-quote-note` → `develop`

## 개요 (Overview)

`gh`로 PR·이슈를 올릴 때 PowerShell에서 제목에 큰따옴표가 있으면 인자가 쪼개져 실패하고, 성공 메시지가 빨간 오류처럼 보이는 점을 가이드에 추가했습니다.

## 관련 이슈 (Related Issues)

- Closes #29

---

## 변경 사항 (Changes)

- `Docs/깃허브 이슈,PR 업로드 가이드.md`: PR 업로드 예시의 `$title`에서 `"`를 `\"`로 바꾸는 처리와 설명 추가, 이슈 제목도 같다는 안내, `✓ Created/Closed` 성공 메시지가 `NativeCommandError`처럼 보일 수 있다는 주의

---

## 스크린샷 / 영상 (Screenshots / Video)

없음(문서 변경)

---

## 테스트 방법 (How to Test)

1. 가이드의 PR 업로드 예시를 큰따옴표가 든 제목으로 실행

**예상 결과:**
`unknown argument` 오류 없이 PR이 만들어짐.

---

## 리뷰 포인트 (Review Focus)

- 안내가 실제 동작(PR #27 제목 `"통신 지연"`)과 맞는지

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [x] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인

### 리뷰어 (Reviewer)
- [ ] 변경 사항 이해 및 의도 파악
- [ ] 코드 로직 검토
- [ ] 직접 빌드하여 테스트
- [ ] 네이밍 / 컨벤션 준수 확인

---

## 추가 메모 (Additional Notes)

- 문서만 변경
