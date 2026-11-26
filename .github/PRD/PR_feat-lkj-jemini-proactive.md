# [FEAT] 제미니 선제 제안·한 단어 명령·WARDEN 코치, 규칙 튜토리얼 시점 변경

- 브랜치: `feat/lkj-jemini-proactive` → `develop`
- 주의: `feat/lkj-warden-coach`(WARDEN 코치) 위에 쌓은 브랜치라 그 커밋도 함께 들어 있음

## 개요 (Overview)

플레이어가 직접 입력해야만 제미니가 움직이던 구조를 바꿔, 전투 중에도 쓰기 쉽게 했습니다. 제미니가 상황을 보고 먼저 제안하고, 짧은 말·버튼 하나로 수락하며, WARDEN 앞에서 막히면 제미니가 단계별 힌트를 줍니다. 규칙(예약) 튜토리얼은 초반 과밀을 피해 맵1 중반으로 늦췄습니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- **선제 제안** (`CXJeminiProactiveComponent`, 고정 문구·LLM 미사용): 탄약 부족(장전), 체력 40% 미만(회복), 여동생 단말기 45초 이상 정체(해킹 대행), 해킹 중 적 접근(미끼). 쿨다운(종류 30초·전체 8초), 대답 제한 15초
- **빠른 수락/거절**: 키보드 `G`/`H`, 게임패드 `LB`/`RB`, 채팅 "응/아니" 등은 서버 없이 로컬 처리. HUD에 `OFFER ... [G] YES [H] NO` 표시
- **한 단어 명령(채팅)**: 엄호·미끼·힐·장전·보호막·해킹·펄스·재부팅·강화 등을 서버 없이 바로 실행(대상·배터리·BUSY 검사는 기존 규칙)
- **WARDEN 코치**: 막힌 플레이어에게 제미니가 추측하는 말투로 단계별 힌트(거절 3회 이상이면 예시 문장). WARDEN 대사는 시스템 상태 보고체로 변경(서버 `persuasion.py`·프롬프트), HUD "다음 ▶" 한 줄. 서버 변경이라 **Render 재배포 필요**
- **튜토리얼**: 규칙 설명(5단계)을 맵1에서 첫 거미·첫 단말기를 겪은 뒤 조용할 때(60초 이후)로 이동
- 콘솔: `CXJeminiOffer <ammo|hp|stuck|decoy>`, `CXJeminiOfferState`, `CXWardenCoachState`
- 문서: `Docs/Jemini/선제_제안.md`, `WARDEN_설명.md`, 콘솔 명령 목록, 튜토리얼 가이드, 코드 아키텍처

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 서버 테스트: `Jemini`에서 `python -m pytest --tb=line -q` (261개)
2. PIE에서 `CXJeminiOffer ammo` → 말풍선과 HUD 확인 → `G`/`H` 또는 `LB`/`RB`, 채팅 "응"
3. `CXJemini 장전`, `힐`, `엄호` 한 단어 명령
4. `CXGiveEvidence all` → `CXGotoWarden` → `CXWardenSay <문장>`으로 코치 힌트 확인, `CXWardenCoachState`로 단계 확인
5. Map1에서 첫 거미·단말기 뒤 60초 조용할 때 규칙 튜토리얼 확인

**예상 결과:**
제미니가 먼저 제안하고 한 번의 입력으로 실행됨. WARDEN 앞에서 막히면 제미니가 힌트를 줌.

---

## 리뷰 포인트 (Review Focus)

- 새 입력 `G`/`H`/`LB`/`RB`가 다른 입력과 겹치지 않는지, 새 Server RPC(`Server_OfferResponse`)
- 로컬 수락은 서버 검증기를 거치지 않고 단순 대상 검사만 함(예비 배터리 등 서버 규칙 미적용)
- 에디터 클린 로드 확인(새 UPROPERTY/UENUM). PIE 동작은 아직 확인하지 않음

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
