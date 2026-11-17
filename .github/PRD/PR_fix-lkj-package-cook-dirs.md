# [FIX] 패키징 쿠킹 누락 방지(타이틀·컷신 등)와 패키징·콘솔 명령 문서

- 브랜치: `fix/lkj-package-cook-dirs` → `develop`

## 개요 (Overview)

코드가 경로 문자열로 불러오는 에셋(타이틀 키아트·로고, 컷신 이미지 등)은 패키징 때 자동으로 쿠킹되지 않아, 에디터 PIE에서는 보이지만 exe에서는 빠질 수 있었습니다. 해당 폴더를 `DirectoriesToAlwaysCook`에 추가하고, 패키징·배포 체크리스트와 시연용 콘솔 명령 목록 문서를 정리했습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `Config/DefaultGame.ini` `DirectoriesToAlwaysCook`에 추가: `/Game/Project_CX/Title`, `/Game/Project_CX/Cutscene`, `/Game/Project_CX/Characters/Brother`, `/Game/Project_CX/Characters/Drone`, `/Game/Project_CX/Environment/Materials`
- `Docs/Guide/패키징_배포_체크리스트.md`: 빌드 전 점검, 쿠킹 설정, 빌드(에디터·명령줄), 빌드 폴더 점검(`.env`·서버 exe 없음), 압축·전달, 팀원 PC 요구사항, 패키징본 실행 확인, 실행 방법 템플릿, 알려진 한계
- `Docs/Guide/콘솔_명령_목록.md`: 시연 시나리오별 순서(제미니·WARDEN·맵 이동·컷신·튜토리얼)와 전체 `CX…` 명령 표, 엔진 기본 명령
- `CLAUDE.md` 문서 표, `Docs/Guide/빌드_실행.md`에 링크 추가
- 코드 변경 없음(설정과 문서만)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터에서 Platforms → Windows → Package Project로 Development 패키징
2. 만들어진 `Project_CX.exe`를 에디터 없이 실행해 타이틀 화면과 NEW GAME 컷신 확인
3. `~` 콘솔에서 `CXGotoNextMap`, `CXGiveEvidence all` 등 시연 명령 확인

**예상 결과:**
타이틀에 키아트 배경과 로고가 보이고, NEW GAME에서 컷신 5컷과 자막이 나옴. 오빠 총·드론·네온 머티리얼도 보임. 콘솔 명령이 동작함.

---

## 리뷰 포인트 (Review Focus)

- 쿠킹 목록 추가 폴더가 빠짐없이 맞는지, 불필요하게 큰 폴더를 넣지 않았는지(`Characters/Brother`는 애니메이션·메시·재질 포함)
- 패키징본에서 실제로 포함되는지는 아직 확인하지 않음(패키징을 돌려 봐야 함)
- 서버 주소 설정 등은 PR #44(제미니 서버 배포) 기준으로 서술됨

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
