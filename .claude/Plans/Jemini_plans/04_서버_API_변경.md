> `제미니 기능 기획.md`(개요)의 분할 문서. 필요할 때만 읽는다.

## 4. 서버/API 변경 (`Jemini/app/`)

### 4-1. 함수 선언 (`llm.py` FUNCTIONS)

모든 함수 인자: `dialogue`(string, 필수), `heard`(string, 선택). `target` 인자 없음(서버가 채움) — 기존 원칙 유지.

| 함수 | 설명(선언문 요지) | 서버가 채우는 target |
|---|---|---|
| `ReloadSupport` | 오빠 레일건 재장전. 오빠는 이것으로만 재장전 | brother |
| `HackTerminal` | 여동생이 접속한 단말기 퍼즐을 3초 뒤 대신 풂 | sister |
| `HackEnemy` | 적 로봇 1기 즉시 그로기. 적 대응의 기본 선택 | enemy |
| `PulseAttack` | 적 1기 즉시 처치. 비쌈, 명시적 처치 요청일 때만 | enemy |
| `DamageBoost` | 오빠 레일건 피해 30초 증가 | brother |
| `HealSupport` | 오빠·여동생 체력 회복 | both |
| `Decoy` | 드론이 5초간 근처 적들의 추적을 끈다 | enemies |
| `QuickReboot` | 쓰러진 플레이어를 바로 부활 | 쓰러진 사람 |
| `Shield` | 5초 무적 보호막. 인자 `who`(brother/sister, 선택 — 비우면 말한 사람) | brother / sister |
| `Chat` | 잡담·질문·되묻기·거절 | - |

`validator.ALLOWED`와 `allowed_function_names` 10개로 확장. `Shield`만 추가 인자 `who`(enum brother/sister, 선택)를 가진다 — 사람을 지목하는 기능이라 예외. `HackAssist` 삭제(5-1절 매핑 표에서도 제거).

### 4-2. 상태 필드 (`schemas.GameState`, UE `FCXJeminiState`)

| 필드 | 타입 | 상태 | 설명 |
|---|---|---|---|
| `brother_ammo`, `brother_ammo_max`, `brother_reloading` | int/int/bool | 기존 | |
| `brother_hp` | int | 기존 | |
| `brother_hp_max` | int | 신규 | 100 |
| `brother_alive` | bool | 신규 | `UCXPlayerLifeComponent::IsAlive` |
| `sister_hp`, `sister_hp_max`, `sister_alive` | int/int/bool | 신규 | |
| `sister_puzzle` | {type, progress} \| null | 기존 | type terminal / enemy |
| `enemy_alive_count` | int | 신규 | 범위 안 살아 있는 적 수(그로기 포함) |
| `enemy_hackable_count` | int | 신규 | 그중 해킹 가능한 수 |
| `enemy_nearest_dist` | float \| null | 신규 | 가장 가까운 적까지 거리(m, LLM 설명용) |
| `terminal_locked_count` | int | 신규 | 맵에 남은 잠긴 단말기 수(0이면 HackTerminal 거절) |
| `damage_boost_remaining` | float | 신규 | 0이면 비활성(LLM이 "연장"이라고 말할 수 있게) |
| `speaker` | "brother" \| "sister" | 신규 | 말한 사람(보호막 기본 대상, 대사 호칭) |
| `brother_respawn_remaining`, `sister_respawn_remaining` | float | 신규 | 부활 대기 남은 초, 대기 아니면 0 |
| `enemy_active_count` | int | 신규 | 15m 안 움직이는(그로기 아닌) 적 수 — 미끼 판정 |
| `brother_shield_remaining`, `sister_shield_remaining` | float | 신규 | 보호막 남은 초 |
| `battery` | int | 기존 | |

`describe_state()`에 줄 추가: "적: 범위 안 3기(해킹 가능 2, 가장 가까운 6m)", "오빠 체력 70/100", "공격력 강화: 18초 남음 / 없음".

### 4-3. 비용표 (`config.py` 환경변수)

| 변수 | 값 | 비고 |
|---|---|---|
| `COST_RELOAD` | 5 | 기존 |
| `COST_HACK_TERMINAL` | 20 | 기존 |
| `COST_HACK_ENEMY` | **20** | 기존 10 → 20 |
| `COST_PULSE_ATTACK` | 50 | 신규 |
| `COST_DAMAGE_BOOST` | 10 | 신규 |
| `COST_HEAL` | 30 | 신규 |
| `COST_DECOY` | 15 | 신규 |
| `COST_QUICK_REBOOT` | 30 | 신규 |
| `COST_SHIELD` | 25 | 신규 |

### 4-4. 검증표 (`validator.validate`, 위에서부터 순서대로)

| 함수 | 검사 → 거절 사유 |
|---|---|
| ReloadSupport | `brother_reloading` → `ALREADY_RELOADING` / 탄 가득 → `AMMO_FULL` |
| HackTerminal | 여동생 단말기 세션 없음 **그리고** `terminal_locked_count == 0` → `NO_TERMINAL` |
| HackEnemy | `sister_puzzle.type == enemy`면 통과 / 아니면 `enemy_alive_count == 0` → `NO_ENEMY`, `enemy_hackable_count == 0` → `ENEMY_NOT_HACKABLE` |
| PulseAttack | `enemy_alive_count == 0` → `NO_ENEMY` |
| DamageBoost | `!brother_alive` → `BROTHER_DOWN` (강화 중이면 거절 없이 연장) |
| HealSupport | 살아 있는 사람 중 hp < max가 없음 → `HP_FULL` |
| Decoy | `enemy_active_count == 0` → `NO_ENEMY` |
| QuickReboot | 부활 대기 중인 사람 없음 → `NO_DOWNED_PLAYER` |
| Shield | 대상(`who` 또는 `speaker`)이 쓰러짐 → `TARGET_DOWN` |
| 공통 마지막 | `battery < 비용` → `LOW_BATTERY` |
| Chat | 검사·차감 없음 |

신규 사유 코드: `NO_TERMINAL`, `NO_ENEMY`, `ENEMY_NOT_HACKABLE`, `BROTHER_DOWN`, `HP_FULL`, `NO_DOWNED_PLAYER`, `TARGET_DOWN` (`NO_PUZZLE`은 `NO_TERMINAL`로 대체, `BUFF_ACTIVE`는 연장 결정으로 없음).

**배터리 차감 위치 변경**: 서버는 `battery_after`를 "실행되면 남을 값"으로 돌려주고, **UE가 효과를 실제로 실행한 순간에 반영**한다(5-4절). 서버는 상태를 저장하지 않으므로 서버 쪽 변경은 없음. 각 코드마다 `DEFAULT_REJECT_LINES` 1~2개, 기능마다 `DEFAULT_OK_LINES` 2~3개(LLM 대사가 비었을 때).

`schemas.Command`: `name` Literal 6개, `target` Literal에 `enemy`/`both` 추가, `puzzle_type`은 삭제(함수가 나뉘어 불필요).

### 4-5. 시스템 지시문 추가/수정 (`prompts/system_instruction.txt`)

- `[할 수 있는 일]` 9가지로 교체: 재장전(ReloadSupport), 단말기 해킹(HackTerminal), 적 해킹(HackEnemy), 적 공격(PulseAttack), 공격력 강화(DamageBoost), 체력 회복(HealSupport), 미끼(Decoy), 빠른 재부팅(QuickReboot), 보호막(Shield).
- `[선택 규칙]` 신설: 3절 표를 문장으로(적 대응은 해킹 우선, 처치 표현일 때만 공격 / 단말기·적 해킹은 퍼즐 상태로 / 여러 요청은 먼저 말한 것 하나 + 대사로 알림 / 애매하면 Chat 되묻기).
- `[대사]` 추가: 실행 함수의 대사는 "지금 하는 행동"을 말한다("재장전 해드릴게요!"). 효과 수치(배율, 회복량)는 약속하지 않는다.
- `[배터리]` 수정: 실행 후 남을 배터리가 **20% 이하**면 대사에 배터리 부족을 꼭 덧붙인다("...전력이 얼마 안 남았어요"). 예약 배터리는 없으니 사용을 막지는 않는다.
- `[출력]` 함수 목록 10개로 갱신. Shield는 사람을 지목했을 때만 `who`를 채운다.
