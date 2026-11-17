# [FEAT] 오프닝 컷신(5컷, 한글·영어 자막)과 세계관·스토리 설정 문서

- 브랜치: `feat/lkj-opening-cutscene` → `develop`

## 개요 (Overview)

새 게임을 시작할 때 재생되는 오프닝 컷신을 추가했습니다. 키아트 일러스트 5컷에 켄 번스 연출, 한글 자막 아래 영어 자막, 건너뛰기를 넣었습니다. 컷신의 근거가 되는 세계관·스토리 설정 문서도 `Docs/Story/`에 정리했습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `UCXCutsceneWidget`(`UI/Menu/CXCutsceneWidget.*`): 타이틀 위에 올라가는 오버레이. 컷마다 켄 번스(배율·이동), 컷 사이 페이드, 한글(크게)·영어(작게, 아래) 자막, 마지막 컷의 로고 연출. 컷 길이·켄 번스·자막 문구와 타이밍은 `GetOpeningCuts()` 한 곳, 자막 크기·페이드 등은 UPROPERTY
- `CXTitleMenuWidget`: NEW GAME 확인 후 세이브 삭제·튜토리얼 초기화는 그대로 두고, 이어서 컷신을 재생한 뒤 기존 `BeginStartGame`을 실행. CONTINUE는 재생하지 않음
- 시작 연출: 타이틀이 검정으로 페이드된 뒤 짧게 유지하고 첫 컷이 페이드인(`IntroBlackHold`)
- 건너뛰기: Esc / Enter / Space / 패드 A·B·Start 한 번으로 전체 건너뜀(시작 후 0.5초는 입력 무시), 우하단 안내. 텍스처가 없으면 그 컷은 검정 + 자막, 전부 없으면 컷신 없이 바로 시작. 감시 타이머와 6초 안전장치
- 콘솔 `CXCutscene [시작컷 1~5]`: 세이브·맵을 건드리지 않고 컷신만 재생(테스트용)
- 에셋: 원본 `Art/Source/Cutscene/cut1~5.png`, 임포트한 텍스처 `Content/Project_CX/Cutscene/`(약 13MB)
- 문서: `Docs/Story/세계관_스토리_설정.md`, `Docs/Story/오프닝_컷신_초안.md`, `Docs/Guide/오프닝_컷신_가이드.md`, `Docs/TODO.md`·`TODO_Done` 정리
- 헤더 변경 있음(전체 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드 후 `Map_Title` Play
2. 콘솔 `CXCutscene`으로 컷신만 재생(`CXCutscene 4`는 4컷부터)
3. 건너뛰기 키(Esc, Space, 패드 A) 확인
4. 마지막에 실제 NEW GAME으로 흐름 확인(기존 세이브가 삭제되므로 주의)

**예상 결과:**
타이틀이 검정으로 어두워졌다가 컷 1이 나오고, 컷마다 천천히 확대·이동하며 한글·영어 자막이 나옴. 컷 5 끝에 로고가 뜬 뒤 게임이 시작됨. 건너뛰면 짧은 페이드 뒤 바로 시작되고 아래 타이틀 메뉴로 키가 새지 않음.

---

## 리뷰 포인트 (Review Focus)

- NEW GAME 흐름에 끼운 위치(세이브 삭제·튜토리얼 초기화 이후, `BeginStartGame` 이전)와 건너뛰기·실패 시에도 항상 게임이 시작되는지
- 컷신이 `PushMenu` 최상단 메뉴로 올라가 입력이 아래 타이틀 메뉴로 새지 않는지
- 일부 컷 텍스처가 없을 때의 폴백
- 텍스처 `.uasset`(약 13MB)은 바이너리라 diff로 확인할 수 없음. 타이틀 BGM은 컷신 중에도 그대로 재생됨(볼륨 조절 API 없음)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
