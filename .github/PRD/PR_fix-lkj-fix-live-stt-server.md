# [FIX] 제미니 실시간 STT 자막이 말하는 동안 안 뜨는 문제

- 브랜치: `fix/lkj-fix-live-stt-server` → `develop`

## 개요 (Overview)

패드 Y를 누르고 말하는 동안 머리 위 실시간 자막이 안 뜨고, 손을 떼고 제미니 처리가 끝나야 플레이어 말풍선이 뜨던 문제를 고쳤습니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **원인**: 블루투스 헤드셋 소리가 작아(RMS 0.003~0.005) 서버 무음 게이트(0.004)에 걸려 자막 요청이 `text: null`로 돌아옴. UE는 글자가 없으면 말풍선을 그리지 않아 "응답 뒤에만 뜨는" 것처럼 보였음
- **UE**: Y를 누르는 순간 말풍선 표시("듣는 중" + 점 → 받아 적은 글자 → 최종 문장)
- **서버 `app/stt.py`**: 무음 게이트 0.0015로 낮춤, 받아 적기 전 작은 소리 증폭(최대 진폭 0.7, 최대 20배)
- **환각 방지**: 증폭 후 단어 목록 프롬프트를 반복해 지어내던 문제 → 프롬프트를 명령 문장 형태로, 출력 32토큰 제한, 압축률 2.2 초과 구간 버림, 반복 구절 제거
- **진단 로그**: 서버 `Jemini/logs/stt.jsonl`(길이·음량·구간별 결과, 음성 원본 저장 안 함), UE `LogCXJeminiInput`(자막 요청 보냄/꺼짐 이유)

---

## 스크린샷 / 영상 (Screenshots / Video)

| 말하는 중 | 최종 문장 |
|---|---|
| ![listening](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-fix-live-stt-server/Docs/Screenshots/Jemini/live_stt_1_listening.png?raw=true) | ![final](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-fix-live-stt-server/Docs/Screenshots/Jemini/live_stt_3_final_sister.png?raw=true) |

---

## 테스트 방법 (How to Test)

1. 빌드 → Play(제미니 서버는 자동 실행)
2. 마이크 연결 후 패드 Y를 누른 채 "공격력 올려줘"
3. 서버 테스트: `cd Jemini` → `.venv\Scripts\python -m pytest -q`(118개)

**예상 결과:**
Y를 누르자마자 "듣는 중" 말풍선 → 약 1초 뒤부터 받아 적은 글자 → 손을 떼고 응답이 오면 최종 문장. 말하지 않고 Y만 누르면 없는 문장이 뜨지 않음.

---

## 리뷰 포인트 (Review Focus)

- 실시간 자막은 로컬 Whisper `small`(CPU)이라 중간 글자가 틀릴 수 있음("마이크" → "아이프"). 최종 문장은 Gemini가 전체 녹음을 듣고 정정 — 실시간 자막은 음성 입력 확인용
- 프롬프트 읊기 필터 기준을 6자 → 12자로 올림(명령 문장 프롬프트에서 진짜 명령 하나가 버려지지 않게)

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
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

- 이 브랜치는 `feat/lkj-map2-improve`에서 갈라졌음 — **`feat/lkj-map2-improve` PR이 먼저 머지돼야** 이 PR 변경이 STT 수정만 보임(그 전에는 맵2 커밋 5개가 같이 보임)
- 버그·수정 기록: `Docs/Debug_log.md` #13
