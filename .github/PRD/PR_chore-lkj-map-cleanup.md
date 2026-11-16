# [CHORE] 사용하지 않는 맵 정리

- 브랜치: `chore/lkj-map-cleanup` → `develop`

## 개요 (Overview)

사용하지 않는 맵과 에셋을 지우고, 테스트 맵을 `Maps/Test/`로 옮겼습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- 삭제: `Map3.umap`, `Map5.umap`, `map5_cyberpunk.uasset`, `unreal_backgroundmap5_Mat.uasset`
- `Map3_work.umap`, `Map_test.umap`을 `Maps/Test/`로 옮김(`Maps/` 쪽 파일도 수정됨)
- 코드 변경 없음, 맵·에셋만 변경

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터에서 프로젝트 열기
2. `Map1`, `Map2`, `Map_Title`이 정상적으로 열리는지, 삭제한 맵을 참조하는 오류가 없는지 확인

**예상 결과:**
오류 없이 열림. 삭제한 맵을 참조하는 곳이 없음.

---

## 리뷰 포인트 (Review Focus)

- 삭제한 맵·에셋이 다른 맵이나 코드에서 참조되지 않는지
- `Maps/Test/`로 옮긴 맵과 `Maps/`에 남은 같은 이름 파일이 중복이 아닌지

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
