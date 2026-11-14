# [DOCS] TODO.md 정리: 완료 항목을 TODO_Done 폴더로 분리

- 브랜치: `docs/lkj-todo-restructure` → `develop`

## 개요 (Overview)

`Docs/TODO.md`가 약 104줄·20KB로 길어져 "남은 일"을 찾기 어려워, 열린 항목만 `TODO.md`에 두고 완료 항목은 월별 파일 `Docs/TODO_Done/`으로 옮겼습니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **`Docs/TODO.md`**: 104줄 → 35줄. 열린 항목 10개만 남기고 항목당 한 줄. 맨 위에 운영 규칙 추가(상세는 기획서·`Debug_log.md`로 링크, 끝나면 `TODO_Done/<월>.md`로 이동)
- **`Docs/TODO_Done/2026-11.md`**(신규): 완료 항목 61개를 **원문 그대로** 이동. 옛 "마지막 갱신" 이력 메모도 보존
- **`CLAUDE.md`**: 문서 표에 "남은 작업(열린 항목만)"과 "끝난 작업 기록(월별) `Docs/TODO_Done/`" 추가
- 직전 작업에서 나온 열린 일 3개를 "제미니 고도화 후속"으로 추가(채팅창 스페이스 문제 확인, 규칙 해석 개선, WARDEN 패널 정리)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음(문서 변경)

---

## 테스트 방법 (How to Test)

1. `Docs/TODO.md`가 열린 항목만 한 줄씩 담고 있는지 확인
2. `Docs/TODO_Done/2026-11.md`에 완료 항목 61개가 빠짐없이 있는지 확인(옛 `TODO.md`와 대조)

**예상 결과:**
`TODO.md`가 짧아지고 완료 기록은 월별 파일에 보존됨.

---

## 리뷰 포인트 (Review Focus)

- 완료 항목 원문이 하나도 삭제되지 않고 이동만 됐는지
- 앞으로의 운영 방식(열린 항목만 `TODO.md`, 완료는 월별 파일)이 팀 작업에 맞는지

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
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

- 문서만 변경. 다른 열린 PR(#27~#32)은 `TODO.md`를 수정하지 않아 충돌 가능성 낮음
