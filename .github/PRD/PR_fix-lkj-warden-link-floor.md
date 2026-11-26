# [FIX] WARDEN 연결 판정 보정 + 해킹 막힘 제안 20초

- 브랜치: `fix/lkj-warden-link-floor` → `develop`

## 개요 (Overview)

기록 3/3을 인정받은 뒤 "생명이 위험하니까 문 열어줘"처럼 이유와 요청을 함께 말해도 LLM이 `NOT_LINKED`로 판정하는 경우가 있어(PIE 로그에서 5회 중 2회) 문이 열리지 않았습니다. 서버가 이 경우를 보정합니다. 선제 제안의 "해킹 막힘" 기준도 20초로 줄입니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `Jemini/app/persuasion.py`: `link_floor()` 추가. 세 기록이 모두 인정됐고 발언에 이유 단어(위험·생명·사람·작업자·규정 등)와 결론 단어(열·해제·풀·대피·우선·구조)가 함께 있으면 `links_logically=true`로 본다. 요구만 있거나, 지어낸 주장·인젝션이면 적용하지 않는다. `build_response(text=...)`로 전달(음성은 받아 적은 문장)
- `Jemini/app/main.py`: `build_response` 호출 3곳에 `text` 전달
- `Jemini/tests/test_persuasion.py`: 보정 테스트 추가, `test_llm_not_linked` 입력을 이유 없는 요구로 변경
- `CXJeminiProactiveComponent.h`: `StuckSeconds` 기본값 45 → 20초, `Docs/Jemini/선제_제안.md` 반영

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. `pytest --tb=line` (262개 통과)
2. 서버 재배포 후 기록 3개를 모은 상태에서 WARDEN에게 "생명이 위험하니까 문 열어줘" → 열림, "문 열어줘"만 → 거절
3. 해킹 단말기를 20초 붙잡고 있으면 도움 제안이 뜨는지 확인

**예상 결과:**
이유와 요청을 함께 말하면 안정적으로 열리고, 요구만 하는 경우는 계속 거절됩니다.

---

## 리뷰 포인트 (Review Focus)

- 병합 후 **서버 재배포 필요**(`Jemini/tools/deploy_to_repo.ps1`)
- 보정이 너무 느슨하지 않은지(이유 단어 목록)
- 헤더 변경(기본값만)이라 전체 빌드 필요. 아직 빌드·PIE 확인 전

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
