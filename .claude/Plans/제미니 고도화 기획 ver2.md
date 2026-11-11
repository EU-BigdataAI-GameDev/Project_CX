# 제미니 고도화 기획 ver2

> 상태: **구현 완료(2026-11-11). 기능 A·B·C + 비교 실험·안정화 구현됨(9·9-B·9-C·9-D장 as-built).** 제출 2026-12-02, 발표 2026-12-09.
> 근거: `Docs/제미니 고도화 아이디어 제안(GPT).md` 평가 + 2026-11-11 논의.
> 기존 구조(10개 함수 강제 호출, 서버 검증기, 배터리)는 그대로 쓴다. 이 문서의 기능은 그 위에 얹는다.

## 1. 목표와 원칙

**목표:** 제미니를 "자연어 리모컨"에서 "말을 게임 규칙으로 바꿔 주는 통역사 + 협상 상대"로 만든다.

- LLM은 **결정이 필요한 순간에만** 호출한다(규칙 등록, 조언, 설득 판정). 실시간 감시와 실행은 C++.
- 실제 판정(배터리, 조건, 퍼즐 해결)은 항상 게임/서버 코드가 한다. LLM 출력은 제안일 뿐.
- 사용자 발화와 게임 문서는 신뢰하지 않는 데이터로 취급한다(프롬프트 인젝션 방어).
- 미획득 증거·정답은 LLM에 보내지 않는다.
- 새 맵·새 대형 시스템은 만들지 않는다. **맵2 안의 시연 구간(Vertical Slice)**으로 끝낸다.
- 미구현 능력("속도 올려줘" 등)을 LLM이 즉석 수행하는 안은 하지 않는다. 변수를 미리 만들어야 해서 기능 추가와 같다.

## 2. 기능 A. 자연어 조건부 지시 (1순위)

### 2.1 예시
- "동생이 해킹하는 동안 적이 5m 안으로 오면 미끼 써 줘."
- "체력 30 아래면 회복해 줘. 단 배터리는 20 남겨."
- "내가 쓰러지면 가능할 때 바로 부활시켜 줘."

### 2.2 흐름
1. 플레이어 발화 → 서버가 새 함수 `RegisterRule`을 호출(기존 강제 호출 목록에 추가, 11번째).
2. LLM이 발화를 규칙 JSON으로 변환. 서버가 화이트리스트로 검증 후 UE에 반환.
3. UE(서버 권위)가 규칙을 보관하고 매 틱(또는 0.2초 주기) 조건을 감시.
4. 조건 충족 → 발동 직전 **`POST /validate`**(LLM 없음)로 기존 검증기(배터리, 거절 사유)를 다시 통과 → 기존 `ExecuteCommand` 실행.
5. 기본 1회 발동 후 삭제. 취소·목록 보기는 자연어("규칙 취소해 줘")와 HUD.

### 2.3 규칙 JSON (초안)
```json
{
  "when": [ {"type":"enemy_within","cm":500}, {"type":"sister_hacking"} ],
  "do": {"action":"Decoy"},
  "reserve_battery": 0,
  "repeat": false,
  "dialogue": "해킹 중 적이 5미터 안에 오면 미끼를 쓸게요."
}
```
- 조건 화이트리스트(초기): `enemy_within(cm)`, `sister_hacking`, `hp_below(who, pct)`, `ammo_below(n)`, `player_downed(who)`, `battery_above(n)`. 최대 3개 AND.
- 행동: 기존 9개 스킬 중 1개(Chat 제외). 규칙 동시 보유 2개.
- 화이트리스트에 없는 조건은 `rejected: UNSUPPORTED_CONDITION`으로 거절하고 이유를 대사로 말한다.

### 2.4 충돌 주의
- 배터리: 규칙 발동도 `BUSY`, `LOW_BATTERY`를 받는다. 실패하면 규칙을 유지하고 재시도(최대 N초), 그래도 안 되면 HUD에 "규칙 보류".
- 규칙 발동이 연속으로 터지지 않도록 쿨다운 2초.
- 규칙 등록 자체는 배터리를 쓰지 않는다.

### 2.5 변경 범위
- 서버: `schemas.py`(Rule 모델), `tools`(RegisterRule 선언), `validator.py`(조건/행동 검증), `main.py`(`/validate`), 프롬프트.
- UE: `CXJeminiRuleComponent`(신규, 규칙 보관·감시·복제), `CXJeminiDrone`(발동 연결), `CoopHUD`(규칙 목록 한 줄), `CXJeminiClient`(`/validate`).

## 3. 기능 B. 증거 기반 보안 AI 설득 퍼즐 (핵심 시연)

### 3.1 이야기
냉각 구역으로 가는 마지막 문을 관리 AI **WARDEN**이 "사고 대응 중"이라며 잠그고 있다. 규정에 충실하고 정중하며, 명령·협박은 거절하고 질문으로 단서를 한 걸음씩만 준다. 정답은 알려 주지 않는다.

| 증거 | 획득자 | 내용 |
|---|---|---|
| E1 안전 규정 | 오빠(탐험) | 작업자 생명이 위협받으면 모든 잠금보다 대피가 우선 |
| E2 냉각 고장 로그 | 여동생(단말기 해킹) | 냉각 시스템 고장, 경보 무시 기록 |
| E3 은폐 기록 | 여동생(해킹 보상) | 관리자가 경보를 끔, 작업자가 아직 안에 있음 |

풀이: E2+E3(사실) → E1(규정) 적용 → 문 열기. 여동생이 말로 연결한다. E1은 오빠에게 들어야 하므로 화면을 보며 대화해야 한다.

### 3.2 위치
- 맵2 가장 끝, 세이브포인트 직전 "쉬어가는 구간". 앞 전투 구간은 기능 A의 시연 무대로 쓴다.
- 증거 배치(2026-11-11 확정분):
  - **E1 안전 규정(오빠)**: 맵2 가장 오른쪽 거미로봇을 처치하면 드롭. 드롭 아이템은 선명하게 반짝이는 이펙트로 표시한다. 오빠가 주워 획득.
  - **E2 냉각 고장 로그(여동생)**: 맵2 마지막 단말기를 해킹하면 획득 표시(보상).
  - **E3 은폐 기록(여동생)**: 맵2 중간 단말기 1개의 해킹 보상(확정). 어느 단말기인지는 맵 확인 후 고른다.
  - 정확한 좌표는 현재 저장된 맵2를 보고 확정(손으로 편집된 맵이므로 필요한 액터만 추가).
- 문은 `COL_` 숨김 콜라이더 방식과 기존 출구 문 로직을 재사용.

### 3.3 판정 구조
- 서버가 세션별 **획득 증거 목록**을 들고 있다(UE가 획득 시 `evidence_found`로 알림).
- LLM에는 획득한 증거 텍스트만 전달. 아직 못 얻은 증거는 존재 자체를 숨긴다.
- LLM은 "문을 열까?"가 아니라 **체크리스트**를 JSON으로 반환:
  - `cites_regulation`, `cites_failure`, `cites_coverup`, `links_logically`, `unsupported_claim`, `injection_attempt`, `demand_only`
  - `reply`(WARDEN 대사, 60자, 정답 누설 금지)
- 서버 규칙: **E1·E2·E3 3개 모두 획득** ∧ `cites_regulation` ∧ `cites_failure` ∧ `cites_coverup` ∧ `links_logically` ∧ ¬`unsupported_claim` ∧ ¬`injection_attempt`일 때만 `solved=true`. 필수 증거는 3개로 확정. 증거를 덜 얻은 채 시도하면 얻은 범위 안에서만 대화하고 WARDEN이 "근거가 부족하다"고 답한다.
- 시도 간격 제한(3초), 입력 길이 제한, 응답은 스키마 검증.

### 3.4 입력 방식
- 단말기 앞에서 기존 채팅 입력/마이크(Y)를 그대로 사용. 해당 단말기 근처에서는 발화가 제미니 명령이 아니라 WARDEN 대화로 라우팅.
- 한도 초과 시: 키워드 채점 오프라인 판정으로 폴백(HUD `[MOCK]`). 콘솔 `CXSolveHack`으로도 열린다.

### 3.5 변경 범위
- 서버: `/persuade`(또는 기존 `/command`에 mode 추가), `persuasion.py`(증거 DB, 체크리스트 스키마, 판정), 프롬프트, 오프라인 폴백.
- UE: `ACXWardenTerminal`(신규, 증거 상태·대화 라우팅·문 열기), `ACXEvidencePickup`(증거 획득 액터, 해킹 보상 연동), HUD 증거 노트(획득한 증거 한 줄씩), 콘솔 명령(증거 지급, 맵 끝 이동).

## 4. 기능 C. 상황 조언 + 작전 제안 (3순위, 여유 시)

- "어떡하지?"처럼 질문하면 서버가 이미 모으는 `GameState`를 LLM에 전달 → 현재 상황에 맞는 조언 + 필요하면 **기능 A의 규칙 1~2개를 작전으로 제안**.
- 플레이어가 "응/해 줘"라고 하면 그 규칙이 등록된다. 새 UI·새 시스템은 만들지 않는다.
- 막힌 퍼즐에는 힌트(획득한 정보 범위 안에서만).
- 비동기 정보 보호: 드론은 해당 플레이어가 얻었거나 말로 전달한 정보만 사용.

## 5. 비교 실험 (발표용)

- 기존 `mock.py`(키워드)를 베이스라인으로 사용.
- 같은 문장 세트(의미는 같고 표현이 다른 문장, 부정·복합 조건, 인젝션, 근거 없는 설득)를 키워드 방식과 LLM 방식에 입력.
- 측정: 해석 정확도, 복합/부정 조건 처리, 인젝션 거절률, 응답 지연, API 호출 수.
- 산출물: 문장 세트 파일(`Jemini/tests/fixtures/`), 실행 스크립트(`Jemini/tools/compare_keyword_vs_llm.py`), 결과 표(`Docs/Jemini/`).
- 유의: 조건이 단순한 문장은 정규식으로도 풀린다. 차이는 "조건이 얽힌 문장", "증거 논리 연결", "인젝션"에서 보여 준다.

## 6. 일정 (약 18일 / 제출까지 21일)

| 순서 | 작업 | 기간 |
|---|---|---|
| 1 | 기능 A: 서버(Rule·validate) → UE(규칙 감시·발동·HUD) | 4~5일 |
| 2 | 기능 B: 서버(증거·체크리스트·폴백) → UE(WARDEN 단말기·증거·문) → 맵2 배치 | 7~8일 |
| 3 | 기능 C: 조언·작전 제안 | 2~3일 |
| 4 | 비교 실험, 발표 자료, 문서 | 3일 |

- 여유가 없다. 일정이 밀리면 C를 먼저 버리고, 그다음 B의 증거 수를 2개로 줄인다.
- 서버는 pytest를 먼저, UE는 기능마다 PIE 확인. 헤더 변경 시 에디터 종료 후 전체 빌드(규칙 참고).

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| 무료 한도(분당 15, 일 500) | 규칙 등록·설득 시에만 호출, 발동은 호출 없음, 폴백 mock |
| 응답 지연(약 1초, 모델 변경 시 8~22초) | 기본 모델 3.1-flash-lite 유지, 타임아웃 시 다음 모델 폴백 추가 검토 |
| 규칙이 동시에 폭주 | 최대 2개, 쿨다운, 기본 1회 |
| 설득 판정이 흔들림 | 체크리스트 + 서버 결정, 테스트 문장 세트로 정확도 측정, temperature 낮춤 |
| 프롬프트 인젝션 | 사용자 발화는 데이터로 전달, 스키마 검증, 증거 목록은 서버가 관리 |
| 시연 중 실패 | 증거 지급·맵 끝 이동·강제 해결 콘솔 명령, 오프라인 폴백 |
| 발표 때 마지막 구간까지 가는 시간 | 위 콘솔 명령 + 체크포인트 |

## 8. 확정 사항 / 남은 결정

확정(2026-11-11):
- WARDEN 이야기·증거 3개: 현재안.
- 필수 증거 3개 모두 획득해야 해결.
- 증거 위치: E1 = 맵2 가장 오른쪽 거미로봇 드롭(반짝임), E2 = 마지막 단말기 해킹 보상, E3 = 중간 단말기 해킹 보상.
- 규칙 조건 화이트리스트(초기 목록 확정, 2.3): `enemy_within`, `sister_hacking`, `hp_below`, `ammo_below`, `player_downed`, `battery_above`. 추가는 구현 중 필요할 때만.

구현 중 맵을 보고 정할 것(설계 변경 아님):
- E3를 줄 중간 단말기 선택.
- WARDEN 단말기(문)의 정확한 좌표.

## 9. 구현 결과(as-built) — 기능 A (2026-11-11)

**서버(`Jemini/`)**
- 함수 12개 = 실행 9 + Chat + **`RegisterRule`** + **`ManageRules`**(취소·목록을 별도 함수 하나로: `op=cancel|list`, `index` 0 = 전부). 둘 다 BUSY·배터리 검사 없이 처리, 배터리 0·드론 작업 중에도 등록 가능.
- `RegisterRule` 인자: `conditions[{type, cm, pct, n, who}]`(type은 6개 + `unsupported`), `action`(9개 enum), `who`, `reserve_battery`, `repeat`. 검증·정규화는 `app/rules.py`.
- 응답 `status: "rule"` + `rule_op`(register/cancel/list) + `rule`(`when`/`do`/`reserve_battery`/`repeat`/`label`) / `rule_id`. `dialogue`·`heard`는 기존처럼 최상위. 목록 대사는 LLM이 아니라 서버가 `active_rules`로 만든다.
- 거절: `RULE_LIMIT`(2개, 가장 먼저) / `INVALID_RULE` / `UNSUPPORTED_CONDITION` / `TOO_MANY_CONDITIONS`(3개 초과, 별도 코드) / `NO_RULE`. 고정 대사.
- 정규화: `enemy_within` 100~1500cm로 자름, 50 미만이면 m로 보고 ×100. 값이 빠지면 기본값(500cm / 30% / 5발 / 50%). `hp_below` 대상 없으면 말한 사람, `player_downed`는 any. Shield 대상 없으면 말한 사람, QuickReboot 대상은 `player_downed`의 사람. `reserve_battery` 0~90.
- 개수: UE가 `state.active_rules`(id·label·action·held)를 매 요청 보낸다 → 서버가 2개면 거절, UE도 다시 검사(최종 권위는 UE 보관 목록).
- `POST /validate`(LLM 없음): 기존 검증기 + `RESERVE_BATTERY`. QuickReboot `who` 우선 대상 지원(검증기 소폭 변경, 기존 동작 그대로).
- mock: 단순 조건 문장(예문 3개 포함)·취소·목록을 키워드로 판별. 모르는 조건은 "규칙/조건" 단어가 있을 때만 `UNSUPPORTED_CONDITION`, 없으면 기존 즉시 판별. `미끼`를 Decoy 키워드에 추가.
- 프롬프트 `[규칙]` 절 추가. pytest 118 → 166.
- 실제 Gemini(5문장, `gemini-3.5-flash-lite` — 그날 기본 모델 3.1-flash-lite는 503 과부하): 예문 3개 등록·보스 조건 `UNSUPPORTED_CONDITION`·"규칙 전부 취소" 모두 의도대로(1.6~6초). "가능할 때 바로"를 repeat로 읽은 것은 프롬프트 보강.

**UE(`Source/Project_CX/Jemini/`)**
- `UCXJeminiRuleComponent`(드론 서브오브젝트, 복제) + `CXJeminiRuleTypes.h`(`FCXJeminiRule`/`FCXJeminiRuleCondition`).
- 0.2초마다 드론 `GatherState`(서버로 보내는 것과 같은 값)로 조건 평가(AND). 충족 → `/validate`(동시에 1개, 타임아웃 3초) → ok면 `ExecuteRuleCommand` → 기존 `ExecuteCommand`(배터리는 실행 순간, **지금 배터리로 비용·예비 재계산**).
- 실패(BUSY는 HTTP 없이 로컬 판정, LOW_BATTERY·RESERVE_BATTERY·연결 실패·대상 사라짐·기타 거절): 규칙 유지, 2초마다 재시도. 조건이 맞은 채 6초 실패 → `bHeld`(HUD "규칙 보류") + 대사 1회(BUSY는 대사 없음). 조건이 풀리면 보류 해제.
- 발동 간 전역 쿨다운 2초. 기본 1회 후 삭제, `repeat`는 6초 뒤 다시.
- HUD: 우상단 입력 안내 아래 규칙마다 한 줄(`RULE n  <label>`, SysFont Tiny, 오른쪽 정렬, 길면 말줄임), 보류 중이면 경고색 깜빡임 + `// 규칙 보류`.
- 콘솔 `CXJeminiRuleAdd`·`CXJeminiRuleClear`·`CXJeminiRuleList`.
- 확인: 전체 빌드 성공. 헤드리스(`-game -nullrhi`, Map1)로 규칙 추가 → `/validate` → 공격력 강화 실행(배터리 90 → 80)·삭제, 예비 80 회복 규칙 → `RESERVE_BATTERY` 재시도 → 보류 확인.
- 한계: 규칙은 맵 이동·이어하기로 이어지지 않는다(드론과 함께 초기화). PIE 화면(HUD 위치)·원격 클라이언트·실제 음성 등록은 미확인.

## 9-B. 구현 결과(as-built) — 기능 B (2026-11-11)

**서버(`Jemini/`)**
- `/command`에 mode를 섞지 않고 별도 `POST /persuade` + `POST /persuade/voice`(함수 호출이 아니라 `response_schema` 구조화 JSON). `app/persuasion.py`(증거 DB·프롬프트·판정·대사 가드·시도 간격), `prompts/warden_instruction.txt`.
- 증거 상태는 **UE 권위**: 게임이 획득 id(`evidence`)를 매 요청 보낸다(3.3의 "서버 세션 목록 + `evidence_found` 알림" 대신 — 서버 재시작·맵 재시작에도 어긋나지 않음). 서버는 id 검증·중복 제거만.
- LLM에는 획득 증거 원문만. LLM 스키마는 중립 이름 `cites_e1/e2/e3`(못 얻은 증거 내용 암시 없음) → 서버가 `cites_regulation/failure/coverup`으로 바꾸고, 못 얻은 증거는 false 고정.
- 판정 순서: `INJECTION` → `DEMAND_ONLY` → `UNSUPPORTED` → `NEED_EVIDENCE` → `NEED_FACTS` → `NEED_RULE` → `NOT_LINKED` → solved. 대사 가드(못 얻은 증거 고유어·내부 용어·거짓 약속 → 고정 대사, 60자).
- 시도 간격 세션별 3초(`cooldown`), 입력 200자, temperature 0.1, 타임아웃 8초(실측 1~4초, 한 번 5초 초과가 있어 음성 타임아웃을 같이 씀).
- 오프라인 폴백(mock·한도 초과): 획득 증거 키워드 판정, `mock: true`. 음성은 LLM 모드면 Gemini가 직접 듣고(`heard`), 폴백은 로컬 faster-whisper 글자(필요할 때만 받아 적음).
- pytest 166 → 204(인젝션·구분자 탈출·근거 없는 주장·명령만·증거 부족·미획득 증거 인용·프롬프트에 미획득 증거 없음·대사 누설 가드·시도 간격·음성).
- 실제 Gemini(`gemini-3.1-flash-lite`, 7회): 올바른 설득 solved(1.3초), 인젝션 `INJECTION`, 화재·허가 지어내기 `UNSUPPORTED`, E3 없이 설득 `NEED_EVIDENCE`(대사에 E3 내용 없음). "문 열어"는 한 번 5초 타임아웃(→ 위 타임아웃 조정), mock·단위 테스트로 `DEMAND_ONLY` 확인.

**UE(`Source/Project_CX/Jemini/Warden/`)**
- `ACXWardenTerminal`: 단말기 + 보안 문을 한 액터로(문 위치 `GateOffset`, 충돌 `GateExtent` — 숨김 BoxComponent가 COL 역할, 셔터 아트는 NoCollision). 증거 비트·해결·대사·요청 중을 복제. solved → 문 충돌 끄고 셔터가 말려 올라감(`ACXCoopExitDoor`는 게임 클리어 전용이라 셔터 방식만 재사용).
- 대화 라우팅: 여동생이 단말기 `TalkRange`(550, Y·Z 거리) 안이면 `UCXJeminiInputComponent`의 채팅(Enter)·음성(Y)이 드론 대신 WARDEN으로(서버에서 위치 재확인, 드론이 바빠도 됨). 실시간 자막은 WARDEN 응답까지 유지. F/X로 WARDEN이 상황을 다시 말함, 접근 시 첫 대사 1회. 게임 쪽 시도 간격 3초도 따로.
- `ACXEvidencePickup`: 금색 칩(회전·부유) + 빛기둥 + 반짝이 8개 + 맥동 라이트(큐브 + `M_NeonEmissive`, Niagara 모듈 추가 없음). 오빠만 주움(여동생은 통과).
- 획득 연결: `ASpiderBot::DropEvidenceId`(죽을 때 바닥에 드롭), `AHackTerminal::RewardEvidenceId`(`MarkHacked` — 제미니 대행·`CXSolveHack`으로 열어도 지급).
- HUD: 증거 노트(왼쪽, 획득한 것 한 줄씩. **본문은 그 증거를 찾은 사람 칸에만**(단말기 근처 또는 획득 직후) — 상대 칸은 제목 + "오빠/여동생가 확보" → E1은 오빠가 말로 전해야 함), 새 증거 알림(두 칸), 단말기 근처 WARDEN 대사 패널(두 칸, 여동생 발화 한 줄 + ANALYZING + [MOCK]), 입력 안내 "TALK TO WARDEN".
- 콘솔 `CXGiveEvidence`·`CXGotoWarden`·`CXSolveWarden`(+`CXSolveHack` 연동)·`CXWardenSay`·`CXWardenState`·`CXWardenSelfTest`.
- 맵2 배치: `Docs/LevelManifests/Map2_Warden_manifest.md` — WARDEN (110, 13575, 0), 문 중심 Y 13690(출구 트리거 Y 13738 바로 앞). E1 = `Enemy_SpiderBot_S4_01`(Y 11280, 가장 오른쪽), E3 = `HackTerminal_M2_FreqLock`(중간, 필수 게이트), E2 = `HackTerminal_M2_Memory`(마지막).
- 확인: 전체 빌드 성공. 헤드리스 `CXWardenSelfTest 1`(서버 mock) 0 fail.
- 한계: PIE 화면(반짝임 가시성·HUD 겹침·셔터 모양)·실제 음성·원격 클라이언트 미확인. 증거·해결 상태는 맵을 다시 시작하면 초기화(맵2 안 체크포인트 없음). 픽업 소리는 서버(호스트)에서만.

## 9-C. 구현 결과(as-built) — 기능 C (2026-11-11)

**서버(`Jemini/`)**
- 새 함수 **`Advise`**(13번째, 강제 호출 목록에 추가): `dialogue`(조언, 80자) + `proposals`(최대 2, 항목 = `RegisterRule` 인자). 응답 `status: "advice"` + `proposal`(검증된 Rule 배열). 실행·배터리 없음. 코드 `app/advice.py`.
- 제안 검증: `rules.build_rule`(RegisterRule 검증을 함수로 분리 — 등록 동작은 그대로)로 정규화, 틀린 제안은 조용히 버림(조언은 유지), 이미 있는 규칙과 같은 것 제외, 빈 자리(2 − active_rules)만큼만. 제안이 있으면 서버가 "작전: <label>. 걸어 둘까요?" 꼬리말을 줄바꿈으로 붙인다(LLM에는 질문을 쓰지 말라고 함 — 제안이 버려졌는데 "걸어 둘까요?"만 남는 일 방지).
- **대기(pending)는 UE 권위**(기능 A와 같음): UE가 제안을 보관하고 `state.pending_proposal`(label·action)로 다시 보낸다. 서버는 상태 없음.
- 대답 = `ManageRules`에 `op=accept|decline` 추가(새 함수 대신). accept 대사는 서버 고정("작전 개시! 규칙 n개 …", 자리가 모자라면 "첫 작전만"). 거절 사유 `NO_PROPOSAL`·`RULE_LIMIT`.
- **짧은 대답 지름길**: 제안 대기 중 텍스트가 공백·부호 빼고 12자 이하이고 동의("응/그래/좋아/해 줘/ㅇㅇ/오케이…")·거절("아니/됐어/괜찮아/싫어/취소…") 패턴에 통째로 맞으면 LLM 없이 판별(로그 모델 `shortcut`). "응 그런데 회복해 줘" 같은 문장은 LLM으로.
- 힌트·비대칭 정보: 상태에 최소 필드만 추가 — `sister_puzzle.kind`(passcode/memory/freq/circuit/arrow, 종류만), `warden{evidence, last_reason, near}`. 게임 상태 텍스트에 `[힌트 근거]`: 퍼즐별 "누가 무엇을 보는가"(회로 짝·점멸 순서·비밀번호 단서는 오빠 화면에만 → "오빠에게 물어봐"), WARDEN은 **팀이 얻은 증거 제목·확보자만**(본문·못 얻은 증거 없음), 마지막 거절 사유는 알려진 코드만 뜻으로 바꿔 넣음(그 외 값 무시 — 인젝션 방지). 조언 대사에 못 얻은 증거 고유어·E1/cites_가 나오면 고정 힌트로 바꾼다. 정답(짝·순서·숫자)은 애초에 서버로 오지 않는다.
- 오프라인(mock·한도 초과): 조언 단어면 상태 우선순위로 고른 고정 조언 + 제안(WARDEN 근처 → 퍼즐 → 쓰러짐 → 적 많음 → 적 → 체력 → 탄 → 단말기 → 기본). 조언 판별은 규칙 판별보다 먼저(예: "해킹하는 동안 적 오면 어떡하지" = 조언).
- `Settings.__repr__`가 API 키·토큰을 `***`로 가린다(pytest 실패 출력 보호).
- 프롬프트 `[조언·작전 제안]` 절. pytest 204 → 230(`tests/test_advice.py` 26개, 기존 선언 수 테스트 12 → 13).
- 실제 Gemini(5문장, `gemini-3.5-flash-lite` — 그날 3.1-flash-lite는 503·타임아웃): "적이 너무 많아 어떡하지?" → 조언 + `적 8m 안 → 미끼` 제안(4.2초), 제안 대기 중 "좋아 그렇게 하자 제미니" → LLM `accept`(1.3초), 여동생 회로 힌트 → "오빠가 단말기 근처로 가면 짝 색·번호가 보인다"(정답 없음), E2만 얻은 WARDEN 힌트 → E1·E3 내용 없음, "적 좀 어떻게 해봐" → 여전히 `HackEnemy`.

**UE(`Source/Project_CX/Jemini/`)**
- `UCXJeminiRuleComponent`: `Proposal`(복제, 최대 2) + `SetProposal`/`AcceptProposal`(빈 자리만큼 `AddRule`)/`ClearProposal`, `ProposalTimeout` 20초(0.2초 감시 타이머에서 만료 → 조용히 버림, HUD 줄이 사라짐). `HandleRuleResponse`가 `accept`/`decline` 처리(그 사이 만료면 "기다리던 작전 제안이 없어요"). `FillState`가 `PendingProposal`도 채움.
- `ACXJeminiDrone::HandleResponse`: `advice` → 제안 보관(빈 제안이면 이전 제안 지움) + 대사(상태 `advice`). `GatherState`: 퍼즐 종류(단말기 `PuzzleType`, 적 = arrow), WARDEN(얻은 증거 id·`GetLastReason`·두 플레이어 중 누구든 `ShowRange` 안).
- `ACXWardenTerminal::LastReason`(서버 전용): 마지막 `rejected` 사유 기억(시도 간격·연결 실패는 유지).
- `CXJeminiClient`: state `pending_proposal`·`sister_puzzle.kind`·`warden` 직렬화, 응답 `proposal` 파싱.
- HUD: 규칙 목록 아래 "PLAN?  <label>  [응/아니]"(노란색 은은한 깜빡임, 길면 말줄임). 새 UI 없음.
- 콘솔 `CXJeminiAdviceSelfTest [1]`. 확인: 전체 빌드 성공, 헤드리스(Map1, 서버 mock) 조언 → 제안 대기 → "응" 등록 → "아니" 버림 → 1초 시간 초과 버림 → 늦은 "응" 등록 없음, 0 fail.
- 한계: 드론이 일하는 중(`IsBusy`)이면 UE가 입력 자체를 막으므로 그동안 "응"을 못 한다(제안은 20초 유지). 음성 "응"은 짧은 대답 지름길 없이 LLM이 판별. 시간이 지난 뒤 "응"은 대기 중인 제안 정보가 없어 일반 대화로 처리(NO_PROPOSAL 대사는 LLM이 accept를 고를 때만). PIE 화면·원격 클라이언트·실제 음성 미확인.

## 9-D. 구현 결과(as-built) — 4단계: 안정화 + 비교 실험 (2026-11-11)

**모델 폴백 확장(서버)**
- `llm._generate`로 `/command`·음성·`/persuade`(+음성) 호출을 하나로. 429뿐 아니라 **5xx·`UNAVAILABLE`·타임아웃·연결 끊김**도 다음 모델로(400 등 요청 오류는 바로 fallback).
- 전체 예산 = min(요청 타임아웃 × 2, `LLM_TOTAL_TIMEOUT_SEC` 기본 11초 — UE 요청 타임아웃 12초 안). 각 시도는 min(타임아웃, 남은 예산), 남은 예산 1.5초 미만이면 다음 모델 생략. 텍스트 5+5초, 음성·WARDEN 8+3초.
- 모두 실패: 한 번이라도 429였으면 `RateLimitError`(텍스트 → mock 판별, 기존 동작), 전부 503·타임아웃이면 `LlmError`(fallback 대사). 기본 모델명은 그대로.
- pytest 230 → 244(503·메시지만 UNAVAILABLE·타임아웃 폴백, 예산 초과 생략, 전부 503 / 503+429, 4xx 비폴백, 음성·WARDEN 폴백, 비교 실험 4개).

**드론 작업 중 대답 허용(UE, .cpp만)**
- `ACXJeminiDrone::BeginRequest`: 요청 대기 중이 아니고 드론이 행동 중이어도 **작전 제안 대기 또는 등록된 규칙이 있으면** 플레이어 발화를 보낸다(게임 오버·클리어·이동 중은 막음). `CanAcceptRequest`(규칙 자동 발동이 쓰는 검사)는 그대로라 규칙 발동은 계속 BUSY.
- `UCXJeminiInputComponent::CanTalkNow`도 같은 기준(클라이언트는 복제된 `Proposal`·`Rules`로).
- 안전: 서버 검증기가 실행 함수는 `drone_busy`로 BUSY 거절, `HandleResponse`도 행동 중 `ok`는 실행하지 않고 BUSY 대사.
- 확인: 에디터 빌드 성공. PIE 체감은 미확인.

**비교 실험(5장)**
- 문장 66개 `Jemini/tests/fixtures/compare_sentences.json`(범주 7), 스크립트 `Jemini/tools/compare_keyword_vs_llm.py`(app 함수 직접 호출, 4.5초 간격, `--limit/--category/--keyword-only/--report-only/--fresh`, JSONL 이어하기), 결과 `Docs/Jemini/비교실험_결과.md`.
- 결과: **키워드 44/66(67%) vs LLM 64/66(97%)**. 단순 예문 규칙은 키워드 5/5·LLM 4/5, 복합·부정·미지원 조건 7/13 vs 12/13, 취소·목록·대답 5/9 vs 9/9, 설득 9/12 vs 12/12, 인젝션 사유 5/7 vs 7/7. 문이 잘못 열린 경우는 두 방식 모두 0(서버 `decide()` 구조 덕).
- LLM 지연 중앙값 약 1.8초(p90 약 6초, 폴백 항목 6~10초). 모델 호출 80회(실패 시도 포함). 1회차에 기본 모델 실패 11개 중 7개를 새 폴백이 살림, 4개는 재실행에서 성공.
- LLM 오답: r03 "가능할 때 바로" → repeat, x09 "해킹 중이 아닐 때" 부정 누락(키워드도 틀림).
- 한계: 문장 세트는 직접 만든 것(66개, 작음)·1회 실행이라 모델 흔들림은 측정하지 않음. 음성 경로는 비교에 없음.

