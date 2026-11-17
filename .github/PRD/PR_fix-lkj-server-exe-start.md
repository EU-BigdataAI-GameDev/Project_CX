# [FIX] 제미니 서버가 곧바로 종료되면 다음 실행 후보로 넘어가도록 수정

- 브랜치: `fix/lkj-server-exe-start` → `develop`

## 개요 (Overview)

`jemini-server.exe`가 실행 직후 죽는 불완전한 빌드일 때, 게임이 프로세스가 떴다는 것만 보고 성공 처리해 `.venv` 파이썬 후보로 넘어가지 못하던 문제를 고쳤습니다. 이제 띄운 뒤 4초 뒤에 살아 있는지 확인하고, 죽었으면 그 후보를 제외하고 다음 후보를 띄웁니다.

## 관련 이슈 (Related Issues)

- Closes #21

---

## 변경 사항 (Changes)

- **원인**: 비에디터(`-game`/패키징) 실행에서 후보 순서가 `PackagedExe → DevExe → DevPython`이라 깨진 `dist` exe(`_internal/base_library.zip` 없음)가 먼저 선택됨. `CreateProc`은 성공하므로 폴백이 동작하지 않아 제미니·WARDEN이 "신호 끊김/통신 지연"
- **수정**(`UCXJeminiServerSubsystem`): 띄운 뒤 4초 후 `VerifyLaunched`로 프로세스 생존 확인(`FTSTicker`), 죽었으면 `FailedCandidates`에 넣고 다음 후보 실행. 헤더 변경
- **문서**: `Docs/Debug_log.md` #15
- exe는 `Jemini/tools/build_exe.bat`로 다시 빌드해 정상 확인(`dist/`는 gitignore, 각자 PC 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음(로그로 확인)

---

## 테스트 방법 (How to Test)

1. **헤더가 바뀌었으니 에디터를 닫고 전체 빌드**
2. (재현) `Jemini/dist/jemini-server/_internal/base_library.zip`을 임시로 옮겨 exe를 깨뜨린 뒤 게임(`-game`) 실행
3. 로그 `LogCXJeminiServer`에서 "exited shortly after start — trying the next candidate" 후 `.venv` 파이썬 실행 확인, `http://127.0.0.1:8000/health`가 200

**예상 결과:**
깨진 exe가 있어도 `.venv` 서버로 자동 대체되어 제미니·WARDEN이 응답함.

---

## 리뷰 포인트 (Review Focus)

- 후보 순서는 그대로 두고 "곧바로 죽는 후보만 건너뜀"인지
- 4초 확인 중 게임 스레드를 막지 않는지(Ticker 사용)
- 게임 종료 시 Ticker 제거(`Deinitialize`)

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

- `Docs/Debug_log.md`를 #25 PR도 수정함(같은 위치에 항목 추가) — 머지 순서에 따라 충돌 시 두 항목을 모두 남기면 됨
