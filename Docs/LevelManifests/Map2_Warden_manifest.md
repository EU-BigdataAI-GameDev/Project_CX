# Map2_SoulCity2 — 기능 B(WARDEN 설득) 배치 기록 (2026-11-11)

기획: `.claude/Plans/제미니 고도화 기획 ver2.md` 3장. 손으로 편집한 맵이라 **추가 1개 + 기존 3개 액터의 속성 1개씩**만 바꿨다.
에디터 원격 실행(Python)으로 적용, 적용 전후 전체 액터 스냅샷 비교(트랜스폼 변화 0, 삭제 0, 추가 1 — 988 → 989) 후 저장.

## 추가

| 라벨 | 클래스 | 폴더 / 태그 | 위치 | 비고 |
|---|---|---|---|---|
| `WARDEN_M2_CoolingGate` | `ACXWardenTerminal` | `Gameplay/Warden` / `GEN_Map2Warden` | (110, 13575, 0), 회전 0 | 단말기 = 레인 뒤쪽(X 110, 다른 단말기와 같은 줄), 두 바위 소품(`COL_Prop_SM_S_Soul_Flatrock` Y 13383~13517 / `..Flatrock2` Y 13633~13767) 사이 |

- 문(같은 액터의 컴포넌트): `GateOffset` (-162, 115, 0) → 문 중심 월드 (-52, 13690, 0). `GateExtent` (230, 40, 330) → 충돌 X -282~178(레인 X -275~170 전체), Y 13650~13730, Z 0~660. BlockAll·카메라 무시·숨김(COL 역할). 보이는 셔터는 폭(Y) 110, 높이 380, NoCollision.
- 문은 출구 트리거 `CXLevelExit_Map2_ToSaveCorridor`(Y 13738~14162) **바로 앞**이라 해결 전에는 세이브 복도로 갈 수 없다. 바위 Flatrock2(높이 135) 위로도 못 넘는다(문 충돌 660).
- 쉬어가는 구간: 메모리 게이트(Y 12750) → 바위 B 내리막 → 평지 Y 13340~13650. 적 없음. 여동생 대화 범위(`TalkRange` 550, Y·Z 거리) = 대략 Y 13025~14125.

## 속성만 변경(트랜스폼·다른 속성 그대로)

| 라벨 | 위치 | 속성 | 값 | 의미 |
|---|---|---|---|---|
| `Enemy_SpiderBot_S4_01` | (-50, 11280, 853) | `DropEvidenceId` | `E1` | 맵2 가장 오른쪽 거미 → 처치 시 안전 규정 드롭(오빠가 주움) |
| `HackTerminal_M2_FreqLock` (`MARKET_RELAY`) | (110, 9590, 801) | `RewardEvidenceId` | `E3` | 중간 단말기 — 게이트가 필수 동선을 막아 반드시 해킹함 → 은폐 기록 |
| `HackTerminal_M2_Memory` (`CAVE_LIFT_ECHO`) | (110, 12605, 216) | `RewardEvidenceId` | `E2` | 마지막 단말기 → 냉각 고장 로그 |

이전 값은 모두 `None`. 되돌릴 때는 위 3개 속성을 비우고 `WARDEN_M2_CoolingGate` 하나만 지운다.

## 확인

- 헤드리스 `-game -nullrhi` + `CXWardenSelfTest 1`(서버 mock): 해결 전 문이 캡슐 이동을 막음 → 거미 처치 드롭(-50, 11280, 876) → 여동생은 못 줍고 오빠가 주움(E1) → E2 → 증거 부족 설득 거절 → E3 → "문 열어" 거절 → 올바른 설득 해결 → 문 통과. 0 fail.
