# [DOCS] 깃허브 이슈·PR 업로드 가이드 추가

- 브랜치: `docs/lkj-github-issue-pr-guide` → `develop`

## 개요 (Overview)

Claude Code가 GitHub CLI(`gh`)로 이슈와 PR을 올릴 때 따를 규칙을 `Docs/깃허브 이슈,PR 업로드 가이드.md`로 정리하고, `CLAUDE.md`의 "필요할 때만 읽을 문서" 표에서 연결했습니다. 코드 변경은 없습니다.

## 관련 이슈 (Related Issues)

- Closes #22

---

## 변경 사항 (Changes)

- **`Docs/깃허브 이슈,PR 업로드 가이드.md`**(신규)
  - 브랜치당 이슈는 하나, 작업 시작 전에 작성
  - 사소한 버그는 이슈로 만들지 않고, 중요한 버그는 올리기 전에 사용자에게 확인
  - "PR 올려줘" 요청 시 `.github/PRD/` 문서를 그대로 PR에 올림(첫 줄 h1이 PR 제목, 관련 이슈 번호 필수)
  - PR을 올리거나 작업·버그가 끝나면 이슈를 닫음(`develop`으로 머지하므로 `Closes #`가 자동으로 닫지 않음)
  - `gh` 경로와 한국어 본문 업로드(`--body-file`) 주의점
- **`CLAUDE.md`**: 문서 표에 가이드 한 줄 추가

---

## 스크린샷 / 영상 (Screenshots / Video)

없음(문서 변경)

---

## 테스트 방법 (How to Test)

1. 가이드 문서를 읽고 규칙이 의도와 맞는지 확인
2. `gh auth status`로 로그인 확인 후 `gh issue list`가 동작하는지 확인

**예상 결과:**
가이드대로 이슈와 PR을 올릴 수 있음.

---

## 리뷰 포인트 (Review Focus)

- 규칙이 팀의 이슈·PR 운영 방식과 맞는지(브랜치당 이슈 1개, PR 후 이슈 닫기)
- 이슈 템플릿에 있는 라벨 중 저장소에 없는 것(`enhancement`, `art`, `asset-request`, `triage`, `needs-review`) 정리 여부

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

- 이슈 #21(서버 exe 오류)은 테스트로 만든 이슈이며 별도 브랜치에서 처리 예정
