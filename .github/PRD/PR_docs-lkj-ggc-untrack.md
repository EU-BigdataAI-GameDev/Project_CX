# [DOCS] GGC 2026 전시 자료 폴더를 git 추적에서 제외

- 브랜치: `docs/lkj-ggc-untrack` → `develop`

## 개요 (Overview)

GGC 2026 전시용 자료(신청서, 게임 소개, 포스터)는 저장소에 둘 필요가 없어 `Docs/GGC2026/`를 로컬 전용으로 바꿉니다. 포스터는 이미지·폰트 복사본이 들어 있어 용량도 큽니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `.gitignore`: `Docs/GGC2026/` 추가
- `Docs/GGC2026/GGC2026_신청서_작성.md`: 추적에서만 제거(로컬 파일은 유지)
- `Docs/Guide/게임_소개_A4.html`, `게임_소개_A4.pdf`, `게임_소개_요약.md`: 삭제(로컬에서는 `Docs/GGC2026/`로 옮겨 둠)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 머지 후 `git pull` — `Docs/GGC2026/`의 내 파일이 사라지지 않았는지 확인
2. `Docs/GGC2026/`에 새 파일을 만들어도 `git status`에 안 뜨는지 확인

**예상 결과:**
GGC 폴더가 더 이상 변경 목록에 나타나지 않음.

---

## 리뷰 포인트 (Review Focus)

- 팀원이 `Docs/GGC2026/`를 쓰고 있다면, pull 시 추적이 끊긴 파일이 삭제될 수 있음(파일을 다른 곳에 복사해 두면 안전)
- 이미 올라간 과거 기록은 git 히스토리에 남음

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
