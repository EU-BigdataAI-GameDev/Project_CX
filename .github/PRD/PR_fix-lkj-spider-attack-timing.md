# [FIX] 거미로봇 접촉 시 첫 피해가 0.78초 늦게 들어가던 문제

- 브랜치: `fix/lkj-spider-attack-timing` → `develop`

## 개요 (Overview)

거미로봇이 플레이어에 닿고 첫 피해가 들어가기까지 약 0.78초가 걸려 한 걸음만 물러나도 피할 수 있던 문제를 고쳤습니다. 이제 접촉 후 `AttackDamageDelay`(0.1초)에 첫 피해가 들어가고, 이후 기존 1.1초 주기로 반복합니다. `feat/lkj-brother-gun-anim` 위에서 갈라진 브랜치라 그 PR을 먼저 머지해야 이 PR의 변경만 깔끔하게 보입니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- 원인: `ASpiderBot::ServerThink`(Attack 상태)가 `(ChopsDone + ChopImpactFraction) × ChopPeriod`에 `PerformChop`을 호출. `ChopImpactFraction`=17/24, `ChopPeriod`=1.1초라 첫 피해가 ≈0.78초 뒤
- `AttackDamageDelay`(기본 0.1초, `SpiderBot|Stats`) 추가: 첫 타격은 공격 시작 후 0.1초, 이후는 `ChopPeriod` 간격. 피해 30·주기 1.1초는 그대로
- 공격 애니: 시작 위치를 `PlayLength × ChopImpactFraction − AttackDamageDelay × Rate`로 당겨 시각 타격과 피해 시점을 맞춤. 첫 루프의 들어 올리는 동작은 생략, 이후 루프는 기존대로
- `ChopImpactFraction`은 피해 시점이 아니라 애니 위상 맞춤용으로만 사용. 체력·어그로·추적·그로기 자세는 변경 없음
- 헤더 변경 있음(전체 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드 후 Map1 Play
2. 거미로봇에 접촉하고 피해가 들어가는 시점 확인(로그 `SpiderBot: ... chop #N`)

**예상 결과:**
접촉 후 약 0.1초 안에 피해 30이 들어가고, 이후 1.1초 간격으로 반복됨. 플레이어의 1초 피격 무적 때문에 피해가 겹쳐 쌓이지 않음.

---

## 리뷰 포인트 (Review Focus)

- 첫 피해 지연이 0.78초에서 `AttackDamageDelay`로 바뀐 식(`ImpactTime = AttackDamageDelay + ChopsDone × Period`)과 `GetTimeToNextChop`이 같은 식을 쓰는지
- 접촉 범위(`ContactReach` 20cm)는 바꾸지 않아, 닿기 전에 물러나는 회피는 그대로임

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [ ] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
