# [FEAT] 오빠 전용 C++ 애니메이션(점프 버그 수정·조준 팔 포즈)과 RailBlaster 총 장착

- 브랜치: `feat/lkj-brother-gun-anim` → `develop`

## 개요 (Overview)

오빠가 연속 점프할 때 점프 애니가 안 나오고 서 있는 자세로 굳던 문제를 고치기 위해, 여동생처럼 AnimBP 없이 C++로 상태를 전환하는 `UBrotherAnimInstance`를 추가했습니다. 사격 시 오른팔을 올리는 조준 포즈를 같은 클래스로 옮기고, 임시 큐브 총을 실제 모델(RailBlaster)로 교체했습니다. 앞서 올린 PR #35 위에서 이어진 작업입니다.

## 관련 이슈 (Related Issues)

---

## 변경 사항 (Changes)

- `UBrotherAnimInstance`(Idle / Run / Jump, 0.15초 크로스페이드): 공중이면 항상 Jump, 땅에서 뜰 때마다 점프 클립을 처음부터 다시 재생(착지와 같은 프레임의 재점프 포함). `UBrotherCombatComponent::BeginPlay`가 `bUseNativeAnim`(기본 true)일 때 오빠 메시에 지정
- 조준 포즈: 사격 중 오른팔 체인을 Idle 포즈로 블렌드(달리기 팔 스윙 제거) 후 `RightArm`/`RightForeArm` 회전을 더함. 값은 `RightArmAimRotation`, `RightForeArmAimRotation`(Roll/Pitch/Yaw)로 조정
- RailBlaster 총(18k 삼각형, 2K 텍스처)을 `RightHand` 소켓에 장착. `Art/Source/Brother/RailBlaster/`에 게임용 FBX와 시안, `Content/.../Brother/RailBlaster/`에 임포트 결과
- `CXGunTune loc|rot|scale|show`: PIE에서 총 위치·회전·크기를 실시간 조정하는 콘솔 명령(`ALocalCoopGameMode` Exec)
- `Project_CX.Build.cs`: `Variant_BrotherCombat/Animation` 인클루드 경로 추가
- `BP_Brother`: 총 메시·회전·크기, 조준 팔 값 / `ABP_Brother`: 팔 올리기 노드(폴백용으로 유지)
- 문서: `Docs/TODO.md`·`TODO_Done/2026-11.md` 정리, `.gitignore`에 원본 FBX와 텍스처 중복 폴더 제외 추가
- 헤더 변경 있음(전체 빌드)

---

## 스크린샷 / 영상 (Screenshots / Video)

없음

---

## 테스트 방법 (How to Test)

1. 에디터를 닫고 전체 빌드 후 Map1 Play
2. 오빠로 점프를 연타하고, 착지 직후에도 바로 눌러 보기
3. 달리면서 사격, 가만히 서서 사격
4. 콘솔에서 `CXGunTune show`

**예상 결과:**
매 점프마다 점프 모션이 처음부터 재생되고 서 있는 자세로 굳지 않음. 사격 중 오른팔이 올라가고 달리기 팔 스윙이 보이지 않으며, 총이 손에 붙어 있음. `show`가 총의 위치·회전·크기를 화면에 표시함.

---

## 리뷰 포인트 (Review Focus)

- 조준 포즈 회전 계산이 엔진 `Transform (Modify) Bone`의 Bone Space + Add to Existing와 같은지(`BrotherAnimInstance.cpp`의 `ApplyAimPose`)
- `bUseNativeAnim=false`로 기존 `ABP_Brother`로 되돌릴 수 있는지(정리는 확정 후 예정)
- 조준 알파는 발사를 처리하는 머신에서만 계산되어 원격 클라이언트에는 반영되지 않음(로컬 협동 전제)
- `.uasset`(메시·재질·텍스처, `BP_Brother`, `ABP_Brother`)은 바이너리라 diff로 확인할 수 없고, 텍스처 `.uasset`이 합쳐서 약 14MB

---

## PR 체크리스트 (Checklist)

### 제출자 (Author)
- [x] 로컬에서 실행 및 기능 테스트 완료
- [ ] 관련 이슈 번호 연결
- [x] 커밋 메시지 컨벤션 준수
- [x] 불필요한 디버그 로그 / 주석 제거
- [x] 충돌(Conflict) 없음 확인
