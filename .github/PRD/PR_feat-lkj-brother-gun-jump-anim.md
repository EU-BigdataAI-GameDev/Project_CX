# [FEAT] 오빠 사격 중 이동속도 감소·조준 팔 포즈·임시 권총, 베타 종료 안내 문구

- 브랜치: `feat/lkj-brother-gun-jump-anim` → `develop`

## 개요 (Overview)

오빠가 사격하는 동안 이동속도가 느려지고(난이도), 쏠 때 오른팔을 올려 권총을 든 모습이 되며, 세이브 복도 출구 문구를 "베타 테스트 종료 안내"로 바꿨습니다. 앞서 푸시한 `feat/lkj-fire-slow-beta-notice`(이동속도 감소·안내 문구) 위에 쌓은 브랜치라 그 커밋도 포함됩니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `UBrotherCombatComponent`: 사격 시작~마지막 발사 후 `FireMoveSlowTail`(0.3초) 동안 `MaxWalkSpeed`에 `FireMoveSpeedFactor`(0.5)를 곱함. 서버·로컬 각자 발사 시점에 적용
- 조준 알파: `GetFireAimAlpha()` / `GetFireAimAlphaForPawn()`(0~1, 발사 후 `AimHoldTime` 0.8초 유지, 보간 속도 `AimBlendInSpeed/OutSpeed`)
- 임시 권총: 엔진 큐브로 만든 소품을 `RightHand` 소켓에 부착(`GunMesh`, `GunSocketName`, `GunRelativeTransform`으로 교체·조정). 복제하지 않는 시각 소품, 충돌 없음
- `ABP_Brother`: `Aim Alpha` 변수 + `Transform (Modify) Bone`(RightArm, RightForeArm)으로 사격 시 팔을 올림. `BP_Brother`: 총 크기·위치 조정
- `ACoopHUD::DrawFlowNotices`: `NEXT AREA OFFLINE` → 베타 종료 안내(감사 인사, 다음 맵 예고, 설문 안내). `ACXLevelExit::OfflineNoticeTime` 3초 → 8초
- 문서: `Docs/Guide/오빠_조준_포즈_설정.md`(ABP 설정 순서, 연속 점프 시 애니 정지 가설과 확인 방법)
- 헤더 변경 있음(전체 빌드). 연속 점프 시 점프 애니가 안 나오는 문제는 원인을 확정하지 못해 이번에는 고치지 않았습니다

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드 후 Map1 Play
2. 오빠로 이동하면서 사격
3. 세이브 복도 출구에 두 플레이어가 함께 들어가기

**예상 결과:**
사격 중 이동이 절반 속도로 느려지고, 팔이 올라가 손에 작은 블록(임시 권총)을 든 모습이 됨. 마지막 발사 후 약 0.8초 뒤 팔이 내려옴. 출구에서는 "BETA TEST // END OF BUILD"와 한글 안내 3줄이 약 8초 표시됨.

---

## 리뷰 포인트 (Review Focus)

- 속도 감소가 `MaxWalkSpeed`만 바꾸므로 다른 코드가 속도를 동적으로 바꾸면 덮어쓸 수 있음(현재는 해당 코드 없음)
- 원격 클라이언트에서는 팔 올림 값이 계산되지 않음(로컬 협동 전제)
- `ABP_Brother.uasset`, `BP_Brother.uasset`은 바이너리 변경이라 diff로 확인할 수 없음

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
