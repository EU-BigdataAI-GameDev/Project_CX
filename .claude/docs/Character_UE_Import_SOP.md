# 캐릭터 언리얼 임포트 표준 가이드

**적용 범위:** Mixamo 리깅을 거친 모든 캐릭터
**환경:** UE 5.6 (Interchange) / Blender 4.4 이상
**동반 파일:** `blender_ue_prep.py`

`<Character>` 는 캐릭터 이름으로 치환해서 읽으세요. (예: `Brother`, `Sister`)

---

## 0. 절대 규칙 5가지

1. **스켈레탈 메시(SK)가 항상 먼저.** Skeleton 애셋이 있어야 애니메이션이 붙습니다.
2. **애셋을 지울 땐 `_Skeleton`, `_PhysicsAsset`까지 세트로.** 메시만 지우고 리임포트하면 옛 레퍼런스 포즈가 남습니다.
3. **SK 익스포트는 REST 포즈에서.** POSE 상태로 내보내면 그 프레임이 레퍼런스 포즈로 굳습니다.
4. **애니메이션 FBX에 메시를 넣지 않는다.** 넣으면 언리얼이 별도 스켈레탈 메시를 만듭니다.
5. **애니메이션 임포트 시 Skeleton 칸을 반드시 채운다.** 비우면 새 스켈레톤이 생깁니다.

---

## 1. 네이밍 규칙

| 종류 | 이름 | 예시 |
|---|---|---|
| 소스 스켈레탈 메시 FBX | `<Character>_SK.fbx` | `Sister_SK.fbx` |
| 소스 애니메이션 FBX | `<Character>_Anim_<Name>.fbx` | `Sister_Anim_Walk.fbx` |
| UE 스켈레탈 메시 | `<Character>_SK` | |
| UE 스켈레톤 | `<Character>_SK_Skeleton` | 자동 생성 |
| UE 피직스 애셋 | `<Character>_SK_PhysicsAsset` | 자동 생성 |
| UE 애니메이션 | `<Character>_Anim_<Name>` | |

**파일명에 공백을 쓰지 마세요.** 언더스코어를 씁니다.

---

## 2. 폴더 구조

### 소스 (Content 폴더 **바깥**)

```
<Repo>/Art/Source/<Character>/
    <Character>_SK.fbx
    <Character>_SK.fbm/              텍스처 (자동 생성/참조)
    Anim/
        <Character>_Anim_*.fbx
    Raw/                             Mixamo·생성AI 원본 보관
    Backup/
```

원본 FBX를 Content 안에 두면 프로젝트 용량이 커지고 Git에 그대로 커밋됩니다.

### 언리얼 (`.uasset` 만)

```
Content/<Project>/Characters/<Character>/
    <Character>_SK
    <Character>_SK_Skeleton
    <Character>_SK_PhysicsAsset
    Materials/
    Anim/
```

폴더 이동은 **콘텐츠 브라우저 안에서 드래그** → 우클릭 **리다이렉터 픽스업**. 탐색기에서 옮기면 참조가 깨집니다.

---

## 3. Mixamo 다운로드

### 캐릭터 (리그)

- Format: **FBX Binary (.fbx)**
- Pose: **T-pose**
- 애니메이션 선택 없이 다운로드

### 애니메이션

- Format: **FBX Binary (.fbx)**
- Skin: **Without Skin**
- FPS **30** / Keyframe Reduction **none**
- 루프 애니메이션은 **In Place** 체크

### 반드시 지킬 것

> **리그와 애니메이션은 같은 업로드 세션에서 받으세요.**
>
> Mixamo는 업로드한 캐릭터 비율에 맞춰 애니메이션을 개별 조정합니다. 다른 업로드에서 받은 애니메이션은 본 이름이 같아 임포트는 되지만, 팔처럼 회전이 누적되는 부위부터 어긋납니다. 겉보기엔 "팔이 몸통을 가로지르는" 증상으로 나타납니다.

업로드 → 리깅 → **창을 닫지 말고** 그 상태에서 T포즈와 애니메이션을 연달아 다운로드하세요.

---

## 4. Blender 전처리

Mixamo 출력 FBX는 그대로 쓸 수 없습니다. 두 가지 문제가 있습니다.

- **스케일 정보가 비어 있음** → 언리얼에서 실제 크기의 1/100로 들어옴 (보이지 않음)
- **루트 본 없음** → 루트 모션과 IK 리타게팅 불가

### 자동 처리

`blender_ue_prep.py` 의 CONFIG 를 수정하고 실행하면 아래가 한 번에 처리됩니다.

```python
CONFIG = {
    "character":   "Sister",
    "armature":    "Armature",
    "mesh":        "node_0",
    "target_height_m": 1.80,
    "add_root_bone": True,
    "out_dir":     r"C:/.../Art/Source/Sister",
    "export_sk":   True,
    "actions":     [],        # [] 이면 유효한 액션 전부
}
```

원본은 건드리지 않고 `UE_Export` 컬렉션에 복제본을 만들어 작업합니다.

### 수동으로 할 경우

| 단계 | 내용 |
|---|---|
| 1 | 복제본 생성 (아마추어 + 메시, 데이터까지 복사) |
| 2 | 메시 부모 해제 (월드 트랜스폼 유지) |
| 3 | 배율 계산: `S = 목표키 / 레스트키` |
| 4 | 두 오브젝트에 스케일 S 지정 → **회전·스케일 적용** (Ctrl+A) |
| 5 | **액션의 location F커브 전체를 S배 보정** ← 빠뜨리면 애니메이션이 어긋납니다 |
| 6 | 메시 재부모화, ARMATURE 모디파이어 대상 재연결 |
| 7 | 원점에 `root` 본 추가, `mixamorig:Hips`를 자식으로 연결 |

> **5번이 핵심입니다.** 아마추어에 스케일을 적용해도 액션의 location 커브는 자동 보정되지 않습니다. 본 rest 위치만 S배가 되고 포즈 이동값은 그대로라 애니메이션이 무너집니다.

```python
for fc in fcurves:
    if fc.data_path.endswith(".location"):
        for kp in fc.keyframe_points:
            kp.co.y *= S
            kp.handle_left.y *= S
            kp.handle_right.y *= S
        fc.update()
```

회전은 본 로컬 기준이라 보정할 필요가 없습니다.

### 목표 키 참고

| 기준 | 값 |
|---|---|
| UE5 마네킹 (Manny) | 약 180 cm |
| UE 기본 캐릭터 캡슐 | 반높이 88 × 2 = 176 cm |

마네킹용 애님 블루프린트나 IK 리타게팅을 쓸 계획이면 180cm에 맞추는 편이 이후가 편합니다.

---

## 5. Blender 익스포트 설정

**SK와 애니메이션의 설정이 다릅니다.** 특히 `pose_position` 과 `global_scale` 이 반대입니다.

### 공통

```
apply_scale_options   FBX_SCALE_NONE
axis_forward / up     -Z Forward / Y Up
bake_space_transform  False
add_leaf_bones        False
primary/secondary     Y / X
use_armature_deform_only  False
```

### 스켈레탈 메시

```
pose_position         REST          ★ 반드시
object_types          ARMATURE + MESH
bake_anim             False
global_scale          1.0           → 노드 스케일 100 → UE에서 정상 크기
mesh_smooth_type      FACE          → 스무딩 그룹 경고 제거
path_mode             COPY
embed_textures        False         → .fbm 폴더로 복사됨
```

### 애니메이션

```
pose_position         POSE
object_types          ARMATURE 만   ★ 메시 제외
bake_anim             True
  use_all_bones       True
  use_nla_strips      False
  use_all_actions     False
  force_startend_keying  True
  step 1.0 / simplify 0.0
global_scale          0.01          ★ 스케일 트랙 제거의 핵심
path_mode             AUTO
```

### `global_scale=0.01` 이 필요한 이유

Blender FBX 익스포터는 미터→cm 변환값 **100**을 아마추어 노드의 `Lcl Scaling`에 넣습니다. SK에서는 이게 있어야 언리얼이 정상 크기로 읽습니다.

그런데 `bake_anim`은 이 노드까지 베이킹해서 **100배 스케일을 애니메이션 트랙으로 구워버립니다.** 애니메이션을 적용하면 캐릭터가 거대해집니다.

`global_scale=0.01` 을 주면 `0.01 × 100 = 1.0` 이 되어 노드 스케일이 사라지고, 본 좌표는 미터 그대로 유지됩니다.

| 설정 | UnitScale | 노드 스케일 | 본 좌표 | 스케일 트랙 |
|---|---|---|---|---|
| NONE, global 1.0 | 1.0 | **100** | 미터 | **100** ← 거대화 |
| UNITS 또는 ALL | **100** | 1.0 | 미터 | 1.0 |
| **NONE, global 0.01** | **1.0** | **1.0** | **미터** | **1.0** ← 정답 |

`UNITS`/`ALL` 도 트랙은 1.0이 되지만 `UnitScaleFactor=100` 에 의존하게 됩니다. 언리얼이 이 값을 존중하는지 확인되지 않았으므로 쓰지 않습니다.

> **본 좌표는 미터로 유지해야 합니다.** cm로 바꾸면 스켈레톤 대비 이동값이 100배가 되어 메시가 찢어집니다.

---

## 6. 익스포트 후 검증

임포트 전에 FBX를 파싱해서 확인합니다. `blender_ue_prep.py` 가 자동으로 리포트를 출력합니다.

### 스켈레탈 메시

- [ ] `Geometry > 0` / `Cluster = 본 개수` / `BindPose > 0`
- [ ] `AnimationCurve = 0` — 있으면 불필요한 `_Anim` 애셋이 딸려 생성됩니다
- [ ] `LayerElementSmoothing > 0` / `LayerElementNormal > 0`
- [ ] 본 `Lcl Rotation` 이 레스트 포즈 값 (POSE로 구워지지 않았는지)
- [ ] 정점 Y(Up) 범위가 목표 키와 일치

### 애니메이션

- [ ] `Geometry = 0` / `Cluster = 0` / `Skin = 0`
- [ ] `LimbNode` 개수가 SK와 동일
- [ ] **스케일 커브(`AnimationCurveNode` 타입 `S`) 최대값 = 1.0**
- [ ] 아마추어 노드 `Lcl Scaling = 1.0`
- [ ] 본 `Lcl Translation`(길이)이 SK와 일치

### 공통

- [ ] 왕복 임포트 후 원본 대비 편차 < 0.01 cm
- [ ] **팔 각도의 X 부호가 레스트와 동일** — 리그/애니 출처 불일치 탐지
- [ ] 본 계층(부모 관계)이 SK와 완전 일치

---

## 7. 언리얼 임포트

### 7-1. 스켈레탈 메시

`Art/Source/<Character>/<Character>_SK.fbx`

| 탭 | 항목 | 값 |
|---|---|---|
| 일반 | 균등 스케일 오프셋 | **1.0** |
| 일반 | 이동 / 회전 오프셋 | 0, 0, 0 |
| 스켈레탈 메시 | 스켈레탈 메시 임포트 | 체크 |
| 스켈레탈 메시 | 피지컬 애셋 생성 | 체크 |
| 스켈레탈 메시 | **Skeleton** | **None (비움)** |
| 스켈레탈 메시 | 애니메이션만 임포트 | 해제 |
| 스켈레탈 메시 | Normal Import Method | Import Normals |
| 머티리얼 | 머티리얼 임포트 | 체크 |
| 텍스처 | 텍스처 임포트 | 체크 |

> 스케일 오프셋을 100이나 155로 바꾸지 마세요. 배율은 이미 FBX 안에 있습니다.

**결과 확인:** 애셋 7개 (SK / Skeleton / PhysicsAsset / Material / Texture 4)

**Ctrl+S 로 저장.** 저장 전에는 메모리에만 있습니다.

저장 후 `<Character>_SK` 를 더블클릭해 **레퍼런스 포즈가 팔 내린 A포즈인지** 확인합니다. 여기서 이상하면 다음 단계로 넘어가지 마세요.

### 7-2. 애니메이션

`Anim` 폴더를 먼저 만들고 그 안에서 임포트합니다.

| 탭 | 항목 | 값 |
|---|---|---|
| 일반 | 균등 스케일 오프셋 | **1.0** |
| 스켈레탈 메시 | **Skeleton** | **`<Character>_SK_Skeleton`** |
| 스켈레탈 메시 | 애니메이션만 임포트 | 체크 |
| 스켈레탈 메시 | 스켈레탈 메시 임포트 | 해제 |
| Animations | Import Animations / Bone Tracks | 체크 |
| 머티리얼 / 텍스처 | 임포트 | 해제 |

**결과 확인:** AnimSequence **1개만** 생성. 분홍색(SkeletalMesh)이나 하늘색(Skeleton)이 같이 생기면 Skeleton 지정이 안 된 것입니다.

**레벨에 배치하기 전에** AnimSequence를 더블클릭해 재생 확인 → Ctrl+S.

### 7-3. 임포트 직후 필수

**`파이프라인 디폴트 사용` 버튼을 한 번 누릅니다.** (임포트 창 우측 상단)

Interchange는 마지막에 쓴 파이프라인 설정을 ini에 저장해서 다음 임포트에 그대로 적용합니다. 애니메이션용 설정(`스켈레탈 메시 임포트 해제`)이 남으면 **다음 SK 임포트가 사유 없이 즉시 실패**합니다.

---

## 8. 문제 해결

### 임포트가 사유 없이 즉시 실패

1. `파이프라인 디폴트 사용` 클릭 후 재시도
2. 그래도 안 되면 에디터 종료 후 아래 파일 삭제 (에디터 레이아웃만 초기화, 애셋 영향 없음)

```
<Project>/Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini
```

이 파일의 `[Interchange_StackName__Assets__...]` 섹션에서 현재 설정을 직접 확인할 수 있습니다.

```ini
bImportSkeletalMeshes=False    ← 이 값이 False면 SK 임포트가 항상 실패
bImportMaterials=False
bImportTextures=False
```

### 증상별 진단표

| 증상 | 원인 | 조치 |
|---|---|---|
| 임포트 즉시 실패, 로그에 사유 없음 | `bImportSkeletalMeshes=False` | 파이프라인 디폴트 사용 |
| "제공된 소스 데이터에 임포트할 데이터가 없습니다" | 위와 동일 | 파이프라인 디폴트 사용 |
| "importing animation only requires a valid skeleton" | Skeleton 미지정 | Skeleton 지정 |
| "has no skeleton. This needs to be fixed" | Skeleton 애셋만 삭제됨 | SK 전체 삭제 후 재임포트 |
| 임포트 버튼 비활성화 | 애니메이션 전용 FBX인데 스켈레톤 없음 | SK 먼저 임포트 |
| 뷰포트에 안 보임 | 캐릭터가 실제 크기의 1/100 | Blender 스케일 전처리 |
| 팔이 꼬임 / 머리 젖혀짐 | 레퍼런스 포즈 불일치 | SK+Skeleton+Anim 전부 삭제 후 재임포트 |
| 팔이 몸통을 가로지름 (X 부호 반전) | 리그/애니메이션 출처 불일치 | 같은 Mixamo 세션에서 재다운로드 |
| 애니메이션 적용 시 거대화 | 스케일 트랙 100 | `global_scale=0.01`로 재익스포트 |
| 메시가 원점으로 찢어짐 | 본 이동값이 스켈레톤 대비 100배 | 본 좌표를 미터로 되돌림 |
| 애니메이션 임포트 시 SkeletalMesh도 생성 | 애니메이션 FBX에 메시 포함 | `object_types={'ARMATURE'}` |
| 임포트 실패 반복 (액터 참조) | 레벨이 삭제된 애셋을 참조 | 액터 삭제 → 레벨 저장 → 에디터 재시작 |
| "no smoothing group" 경고 | 정상 | 무시 또는 `mesh_smooth_type='FACE'` |

### 로그 확인

```
<Project>/Saved/Logs/<Project>.log
```

검색 키워드:

| 키워드 | 알 수 있는 것 |
|---|---|
| `Interchange start importing` | 어떤 파일을 언제 임포트했는지 |
| `Interchange: Error:` | 실제 실패 사유 |
| `LogInterchangePipeline: Warning:` | 파이프라인 단계 경고 |
| `Force Deleting` + `Asset Name:` | 무엇을 지웠는지 |
| `LogSkeletalMesh: Building` | 의도치 않은 애셋 생성 |
| `LogAnimation: Warning:` | 스켈레톤 누락 |

로그에 사유가 안 남는 경우도 있습니다. **출력 로그 창을 열어두고 재시도**하면 화면에 상세 사유가 뜹니다.

---

## 9. 루트 본에 대하여

`root` 는 발밑 원점에 놓인 웨이트 없는 본입니다. 메시 디폼에 영향을 주지 않습니다.

**필요한 이유**

- Mixamo 리그는 `mixamorig:Hips` 가 최상위라 언리얼 **루트 모션이 동작하지 않습니다**
- UE5 마네킹으로 **IK 리타게팅** 할 때 기준점이 필요합니다

**주의**

언리얼은 애니메이션 임포트 시 본 계층이 스켈레톤과 정확히 일치해야 받아줍니다. SK에 `root` 가 있는데 애니메이션에 없으면 이 에러가 납니다.

```
Mesh contains root bone as root but animation doesn't contain the root track
```

**SK와 애니메이션 양쪽에 동일하게 넣거나, 양쪽 다 넣지 않거나** 둘 중 하나여야 합니다. 나중에 이동 애니메이션을 쓸 계획이면 처음부터 넣는 편이 좋습니다.

스켈레톤에 root를 추가한 뒤 기존 애니메이션을 임포트하면 위 에러가 나므로, **root 관련 변경은 캐릭터와 애니메이션을 함께 재임포트**하는 방식으로만 반영하세요.

---

## 10. 새 캐릭터 체크리스트

```
[ ] Mixamo에서 리그 + 애니메이션을 같은 세션에서 다운로드
[ ] 파일명에 공백 없음
[ ] Blender: blender_ue_prep.py CONFIG 수정 후 실행
[ ] 검증 리포트에서 FAIL 항목 없음 확인
[ ] Content 바깥 Art/Source/<Character>/ 에 출력 확인
[ ] UE: 파이프라인 디폴트 사용 클릭
[ ] UE: <Character>_SK.fbx 임포트 (Skeleton = None)
[ ] 애셋 7개 생성 확인 → Ctrl+S
[ ] SK 더블클릭 → 레퍼런스 포즈 A포즈 확인
[ ] UE: 파이프라인 디폴트 사용 클릭
[ ] UE: Anim 폴더 생성 후 애니메이션 임포트 (Skeleton 지정)
[ ] AnimSequence 1개만 생성 확인 → Ctrl+S
[ ] AnimSequence 더블클릭 → 재생 확인
[ ] 레벨 배치 후 크기·자세 최종 확인
```
