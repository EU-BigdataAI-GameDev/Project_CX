# [FEAT] 제미니 원격 서버 keep-alive (10분마다 /health)

- 브랜치: `feat/lkj-jemini-keepalive` → `develop`

## 개요 (Overview)

Render 무료 서버는 15분 동안 요청이 없으면 잠들어, 깨어나는 동안(30~60초) 제미니 요청이 "신호가 끊겼어요"로 끝납니다. 원격 서버를 쓸 때 게임이 켜져 있는 동안 10분마다 `/health`를 보내 서버가 잠들지 않게 합니다(GGC 전시 등 오래 켜 두는 시연 대비).

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `CXJeminiServerSubsystem`: 시작 시 워밍업 `/health`를 `PingRemote()`로 분리하고, 원격 서버일 때 `KeepAliveInterval`(기본 600초, `DefaultGame.ini`로 조정, 0이면 끔)마다 반복 호출
- 응답은 로그(`LogCXJeminiServer`: `remote keep-alive ok (HTTP 200)`)에만 남김. 로컬 서버 사용 시, 게임 종료 시에는 동작하지 않음/정리됨

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 원격 `ServerUrl`로 게임 실행(타이틀 화면에 두어도 됨)
2. `Saved\Logs`에서 `LogCXJeminiServer`의 `warm-up ok` 및 10분 뒤 `keep-alive ok (HTTP 200)` 확인

**예상 결과:**
게임이 켜져 있는 동안 10분마다 keep-alive 로그가 찍히고, 서버가 잠들지 않아 제미니 첫 응답이 느려지지 않음.

---

## 리뷰 포인트 (Review Focus)

- 헤더 변경(`KeepAliveInterval` 설정, `KeepAliveHandle`)이라 전체 빌드 필요. 에디터·게임 타깃 빌드 성공 확인
- PIE/실행에서 10분 경과 확인은 아직 하지 않음

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
