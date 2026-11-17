# [RELEASE] v0.1-beta — develop을 main에 반영

- 브랜치: `develop` → `main`

## 개요 (Overview)

CAELUM:UNCHAINED 베타(v0.1-beta, 팀 시연 배포판) 시점의 `develop`을 `main`에 반영합니다. main에는 PR·이슈 템플릿만 있었고, develop에 먼저 머지해 충돌(`.gitignore`)을 해결했습니다(파일 내용은 develop과 동일).

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- **레벨**: Map1·Map2(해킹 방어전, 회로 퍼즐, 계단 보정), 세이브 복도, 환경 에셋
- **전투·해킹**: 오빠 자동 조준 사격·전용 C++ 애니메이션·RailBlaster 총, 여동생 해킹 퍼즐(주파수·메모리·회로), 거미로봇 적 해킹
- **제미니 AI 드론**: LLM 서버(FastAPI+Gemini), 기능 9종, 조건부 규칙·조언·실시간 STT, WARDEN 증거 설득 퍼즐, 외부 배포(Render)
- **UI·연출**: 메뉴 5종, 타이틀 키아트, 오프닝 컷신(한/영 자막), 첫 플레이 튜토리얼, 폰트·게임명·아이콘
- **배포·시연**: 패키징 쿠킹 설정·컴파일 오류 수정, 패키징 체크리스트, 시연용 콘솔 명령 목록, 숨김 솔로 모드(`CXSolo`)
- **문서**: 세계관·스토리, 가이드(`Docs/Guide/`), TODO·TODO_Done 정리
- 상세는 머지된 PR #5 ~ #47 참고

---

## 스크린샷 / 영상 (Screenshots / Video)

`Docs/Screenshots/` 참고

---

## 테스트 방법 (How to Test)

1. `main`을 받아 에디터에서 `Map_Title`로 시작
2. 패키징본(Development) 실행: 타이틀 → NEW GAME → 컷신 → Map1 → Map2 → 복도 → 베타 종료 안내
3. 제미니(배포 서버) 응답, WARDEN 설득 확인

**예상 결과:**
패키징·실행이 오류 없이 되고, 팀원이 구글 드라이브 배포판으로 같은 흐름을 플레이할 수 있음.

---

## 리뷰 포인트 (Review Focus)

- `.gitignore`: develop 버전 사용(`.env`·서버 로그·`Sounds/` 제외 규칙 포함)
- `Config/DefaultGame.ini`의 `ServerUrl`·`ApiToken`(배포 서버 접근용 토큰, Gemini 키 아님)
- 머지 방식: 이력 보존을 위해 **Create a merge commit** 권장(Squash 비권장)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
