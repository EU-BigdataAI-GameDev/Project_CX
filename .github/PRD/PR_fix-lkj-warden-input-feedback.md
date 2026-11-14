# [FIX] WARDEN 채팅 입력이 조용히 버려지는 문제 (분석 중·간격·표시)

- 브랜치: `fix/lkj-warden-input-feedback` → `develop`

## 개요 (Overview)

WARDEN이 분석 중이거나 3초 간격 안일 때 보낸 채팅이 아무 표시 없이 버려지던 문제를 고쳤습니다. 이제 채팅창은 분석 중에도 열리고, 마지막 입력 한 개를 보관했다가 응답 뒤에 자동 전송하며, 버려지는 경우는 화면과 로그에 이유를 남깁니다.

## 관련 이슈 (Related Issues)

- Closes #25

---

## 변경 사항 (Changes)

- `ACXWardenTerminal`: 분석 중·간격 안에 온 텍스트는 마지막 한 개를 `QueuedText`로 보관 → 응답 1.5초 뒤(또는 간격 해제 시) 자동 전송. 해결되면 폐기. 대화창에 "분석 중… 접수했습니다" / 간격 초 표시, 거절 사유와 글자 수를 `LogCXWardenTerminal`에 기록. 음성은 보관하지 않고 안내만
- `UCXJeminiInputComponent`: WARDEN 앞에서는 분석 중에도 채팅창이 열림(`CanTalkNow`), 음성은 분석 중이면 안내 후 차단
- 채팅창: WARDEN용 라벨/힌트, `n/200` 카운터(200 초과 시 주황색), 입력·키·포커스·확정 진단 로그(`LogCXJeminiInput`)
- `ACoopHUD::DrawWarden`: 플레이어 인용 46자 → 최대 2줄·약 90자
- 문서: `Docs/Jemini/WARDEN_설명.md`, `Docs/Debug_log.md` #16
- 스페이스 뒤 글자 씹힘은 원인 미확인이라 동작은 바꾸지 않고 로그만 추가했습니다. 헤더 변경 있음(전체 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드. `Jemini` 서버 실행(`127.0.0.1:8000`)
2. `Map2_SoulCity2` Play, 콘솔 `CXGiveEvidence all`, `CXGotoWarden`
3. Enter로 WARDEN에게 말을 보낸 뒤, 분석 중(`ANALYZING`)에 Enter로 한 번 더 보내기(또는 콘솔 `CXWardenSay a` 직후 `CXWardenSay b`)
4. 80자 이상 문장 보내기, 200자 넘게 입력해 보기

**예상 결과:**
두 번째 입력이 "분석 중… 접수했습니다"로 표시되고, 첫 응답 뒤 자동으로 전송됨. 인용이 2줄로 보이고 입력창에 `n/200`이 보임. 로그에 `input queued` / `queued text sent`.

---

## 리뷰 포인트 (Review Focus)

- 보관은 서버(단말기, 권위)에서만 하고 마지막 한 개만 유지(덮어씀), 해결·강제 해결 시 폐기하는지
- 대기 전송이 서버 3초 간격(`PERSUADE_COOLDOWN_SEC`)에 걸리지 않게 클라이언트 간격 + 0.05초 여유를 두는지
- 진단 로그가 채팅이 열린 동안에만 찍히는지(키 입력마다 Log 한 줄)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [x] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [ ] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인

### 리뷰어 (Reviewer)
- [ ] 변경 사항 이해 및 의도 파악
- [ ] 코드 로직 검토
- [ ] 직접 빌드하여 테스트
- [ ] 네이밍 / 컨벤션 준수 확인

---

## 추가 메모 (Additional Notes)

- 확인: 빌드 성공, `-game -nullrhi`에서 `CXWardenSay` 2개 연속 → 두 번째 보관 후 자동 전송. 실제 채팅창 UI·`n/200`·2줄 인용은 PIE에서 육안 확인 필요
- 스페이스 씹힘은 다음 재현 시 로그(`ChatBox text changed/key down/focus`)로 확인. 기록: `Docs/Debug_log.md` #16
