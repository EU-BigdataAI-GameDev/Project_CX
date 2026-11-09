# [FEAT] 맵2 개선(회로 퍼즐·계단·배경음) + 제미니 실시간 STT 자막

- 브랜치: `fix/lkj-fix-live-stt-server` → `develop`

## 개요 (Overview)

맵2에 전용 배경음, 계단 콜라이더 수정, 비밀번호 단말기를 대체하는 회로 연결 퍼즐을 넣었습니다. 제미니 음성(패드 Y)은 말하는 도중 머리 위 말풍선에 받아 적은 글자가 바로 뜨도록 로컬 STT(faster-whisper)를 붙였고, 마이크 사용 후 PIE를 끄면 에디터가 크래시하던 버그를 고쳤습니다.

## 관련 이슈 (Related Issues)

- Closes #
- Related to #

---

## 변경 사항 (Changes)

- **회로 연결 퍼즐**: `EHackPuzzleType::CircuitLink`(6×5 격자, 3~4쌍, 교차 불가). 여동생은 짝을 모르고 오빠가 9m 안에 있으면 오빠 화면에 짝 색·번호 표시. 제미니 대리 해킹·`CXSolveHack` 지원. 맵2 `SHUTTER_CTRL-5` 단말기에 적용, 비밀번호 단서 4개 삭제
- **맵2 계단**: 계단 아트와 걷는 콜라이더 불일치 수정(최대 2.1m 떠서 걷던 마지막 단말기 구간 포함), 손 편집 레이아웃에 맞춰 콜라이더 재배치, 단말기를 경사면으로 내림
- **맵2 배경음**: `SW_BGM_Map2`(101 BPM, 인트로 1회 후 73마디 반복)
- **실시간 STT**: 서버 `POST /stt/partial`(faster-whisper `small`, CPU int8). Y를 누르면 말풍선이 뜨고 0.6초마다 받아 적은 글자로 갱신, 손을 떼면 Gemini가 들은 최종 문장으로 교체. 작은 헤드셋 소리 증폭, 잡음 환각 필터
- **크래시 수정**: 녹음 종료·취소·EndPlay 때 마이크 스트림을 닫아 PIE 종료 GC 중 접근 위반 해결(`Docs/Debug_log.md` #12)

---

## 스크린샷 / 영상 (Screenshots / Video)

| 회로 퍼즐(오빠 중계) | 실시간 자막 | 계단·단말기 구간 |
|---|---|---|
| ![circuit](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-fix-live-stt-server/Docs/Screenshots/CircuitLink/03_relay_linked_drawing.png?raw=true) | ![stt](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-fix-live-stt-server/Docs/Screenshots/Jemini/live_stt_1_listening.png?raw=true) | ![stairs](https://github.com/EU-BigdataAI-GameDev/Project_CX/blob/fix/lkj-fix-live-stt-server/Docs/Screenshots/Map2Stairs/06_S5_user_layout_nocollision_gate_PIE.png?raw=true) |

나머지: `Docs/Screenshots/CircuitLink/`, `Docs/Screenshots/Map2Stairs/`

---

## 테스트 방법 (How to Test)

1. 서버 의존성 갱신: `cd Jemini` → `.venv\Scripts\pip install -r requirements.txt` → `.venv\Scripts\python tools\download_stt_model.py`(모델 약 460MB, 첫 실행 때 자동 다운로드도 됨)
2. 빌드 → `Map2_SoulCity2`에서 Play
3. 회로 퍼즐: `SHUTTER_CTRL-5` 단말기에서 F/X로 시작점→도착점 연결. 오빠를 가까이/멀리 두고 짝 표시 확인
4. 계단: 오빠·여동생으로 모든 계단과 마지막 단말기 구간 오르내리기
5. 실시간 자막: 마이크 연결 후 패드 Y를 누른 채 "공격력 올려줘". 마이크 없이는 콘솔 `CXJeminiVoiceFile "<wav>" [0|1|sister]`
6. 크래시: 녹음 중 / Y를 짧게 누른 직후 PIE 정지 반복
7. 서버 테스트: `cd Jemini` → `.venv\Scripts\python -m pytest -q`(118개)

**예상 결과:**
퍼즐을 모두 이으면 게이트가 열리고, 계단에서 발이 보이는 바닥에 붙어 있으며, Y를 누르자마자 "듣는 중" 말풍선 → 글자 → 최종 문장 순으로 바뀜. PIE를 꺼도 에디터가 튕기지 않음.

---

## 리뷰 포인트 (Review Focus)

- **맵2 손 편집 레벨**: 계단·단말기 구간은 필요한 액터만 수정, 고치기 전 트랜스폼은 `Docs/LevelManifests/`(`map2_stairs_audit.md`)에 기록. 아트 메시는 `NoCollision` 유지, 걷는 판정은 `COL_` 액터
- **새 C++**: `CXCircuitLinkPuzzle`, `HackTerminal`·`SisterHackComponent`(회로 세션 RPC), `CXJeminiVoice`(스트림 닫기), `CXJeminiInputComponent`(실시간 자막)
- **서버 용량**: STT 모델 `Jemini/models/`는 gitignore. 서버 exe 폴더는 모델 포함 약 680MB
- 실시간 자막은 로컬 Whisper라 정확도가 Gemini 최종 문장보다 낮음(확인용). 원격 네트워크 클라이언트는 최종 말풍선만

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

- `feat/lkj-map2-improve` 커밋(develop 미머지)도 포함 — 이 PR 하나로 함께 머지
- 진단 로그: 서버 `Jemini/logs/stt.jsonl`, UE `LogCXJeminiInput`(자막 요청 보냄/꺼짐 이유)
- 미확인: 원격 클라이언트 경로, GPU STT(현재 CPU `small`)
- 버그·수정 기록: `Docs/Debug_log.md` #11~#13, 진행도: `Docs/TODO.md`
