# Hunyuan3D FBX → Mixamo 업로드 가이드

**대상:** Hunyuan3D로 생성한 캐릭터 FBX를 Mixamo 자동 리깅(Auto-Rigger)에 올릴 때
**환경:** Blender 5.1 / Mixamo Auto-Rigger
**검증 사례:** Project_CX Sister 캐릭터 (2026-09-27 업로드 성공)

---

## 1. 증상

Hunyuan3D 출력 FBX를 Mixamo에 올리면 아래 오류가 나고 리깅 단계로 넘어가지 않습니다.

```
AUTO-RIGGER
Sorry, unable to map your existing skeleton.
Please check best practices for using the Auto-Rigger and upload again.
```

> **주의: 메시지와 실제 원인이 다릅니다.**
> "기존 스켈레톤을 매핑할 수 없다"고 하지만, 파일 안에 뼈대는 **없습니다.** Sister 원본과 업로드 파일 모두 `LimbNode 0`, `Deformer 0`, `Pose 0`임을 FBX 바이너리를 직접 파싱해 확인했습니다. "뼈대가 없어서" 나는 오류도 아닙니다. Mixamo 자동 리깅은 뼈대 없는 메시를 받는 게 정상입니다.

---

## 2. Hunyuan3D 원본 FBX의 특징

Sister 원본(`Sister_TPose.fbx`) 분석 결과입니다. Brother 원본(`Brother_model.fbx`, 93.9 MB)도 같은 구조였습니다.

| 항목 | 값 | 문제 여부 |
|---|---|---|
| 파일 크기 | **93.79 MB** | 과대 |
| 삼각형 | **999,360** (정점 499,402) | 과다 |
| 텍스처 | 4096×4096 PNG **4장 임베드** (BaseColor / Normal / Metallic / Roughness) | **메탈릭·러프니스 연결이 원인** |
| 텍스처 파일명 | `texture_pbr_20250901*.png` | 캐릭터마다 동일한 이름 (충돌 위험) |
| 오브젝트 회전 | X축 90° | 적용 필요 |
| 스무딩 그룹 | 없음 | 경고 원인 |
| 뼈대 | 없음 | 정상 |
| 메시 상태 | 섬 1개, 비매니폴드 엣지 1개, 발밑 Z=0 | 양호 |

---

## 3. 원인 분석

Mixamo 커뮤니티(Adobe Community, Blender Artists)에 이 오류가 **뼈대 없는 메시에서도** 나는 사례가 다수 보고되어 있습니다. 보고된 원인은 세 가지입니다.

| 원인 | 보고 내용 | Sister 해당 여부 |
|---|---|---|
| **셰이더에 메탈릭·러프니스 연결** | BaseColor와 Normal 외의 맵이 BSDF에 연결되어 있으면 업로드 실패 | **해당** — 4장 전부 연결 |
| **파일 용량 과다** | 78 MB 파일이 15 MB로 줄이자 동작 | **해당** — 93.8 MB |
| 발이 지면 아래로 파묻힘 | 모델을 올려 발을 지면에 맞추자 해결 | 해당 없음 — 발밑 Z = 0.0000 |

### 실제 경과

| 시도 | 폴리곤 | 크기 | 텍스처 연결 | 결과 |
|---|---|---|---|---|
| 1차 | 50,000 | 12.87 MB | 4장 (메탈릭·러프니스 포함), 2K | **실패** (같은 오류) |
| 2차 | 50,000 | 10.60 MB / 7.70 MB | BaseColor + Normal / BaseColor만, 2K | **성공** |

1차에서 폴리곤과 용량을 이미 줄였는데도 실패했고, **메탈릭·러프니스 연결만 제거한 2차에서 성공**했습니다. 따라서 이 사례의 결정적 원인은 **메탈릭·러프니스 텍스처 연결**로 판단합니다. 용량 축소는 필요조건이었는지 이 사례만으로는 분리할 수 없으나, 업로드 속도와 안정성을 위해 함께 적용하는 것을 권장합니다.

> Brother 캐릭터 업로드 당시 오류도 같은 원본 구조(94 MB, 텍스처 4장 연결)였으므로 같은 원인이었을 가능성이 높습니다. 다만 당시 오류 내용은 기록이 없어 확인되지 않았습니다.

---

## 4. 해결 절차 (Blender)

### 4-1. 임포트 및 복제

1. 빈 `.blend` 파일에 원본 FBX 임포트
2. 원본 메시는 이름을 바꿔 숨겨두고(`<Character>_HighRes_SRC`), **복제본에서 작업**

### 4-2. 트랜스폼 적용

- 오브젝트 회전 X 90° → **Ctrl+A → Rotation & Scale** 적용
- 결과: rot (0,0,0) / scale (1,1,1)

### 4-3. 폴리곤 축소 (데시메이트)

| 설정 | 값 |
|---|---|
| Modifier | Decimate |
| Type | **Collapse** |
| Ratio | `목표 삼각형 수 / 현재 삼각형 수` (Sister: 50,000 / 999,360 ≈ 0.05) |
| Triangulate | 체크 |
| Symmetry | 체크, Axis **X** |

적용 후 확인:
- 삼각형 **50,000** (정점 약 24,700)
- 키·폭이 원본과 동일한지 (Sister: 키 1.1808 m / 팔 폭 0.9488 m)
- 비매니폴드 엣지, 떠 있는 정점, 면적 0인 면 개수
- **뷰포트로 얼굴·손가락·옷 디테일 육안 확인**

> 이 작업은 **데시메이트(자동 감면)** 이지 리토폴리지가 아닙니다. 관절 부위 엣지 루프가 정리되지 않아 팔꿈치·무릎 변형이 다소 거칠 수 있습니다. 애니메이션에서 관절이 심하게 찌그러지면 그때 리토폴리지를 고려합니다.

### 4-4. 텍스처 정리

1. 임베드된 4K 텍스처 4장을 디스크로 꺼내며 **캐릭터 이름으로 변경**

```
texture_pbr_20250901.png            → <Character>_BaseColor.png
texture_pbr_20250901_normal.png     → <Character>_Normal.png
texture_pbr_20250901_metallic.png   → <Character>_Metallic.png
texture_pbr_20250901_roughness.png  → <Character>_Roughness.png
```

Hunyuan3D는 캐릭터가 달라도 **같은 파일명**으로 텍스처를 내보냅니다. 이름을 바꾸지 않으면 언리얼에서 캐릭터 간 텍스처가 섞일 수 있습니다.

2. 저장 위치
   - `Art/Source/<Character>/Textures/` — **4K 원본. 언리얼용. 반드시 보관**
   - `Art/Source/<Character>/Mixamo_Upload/Textures/` — **2K 축소본. 업로드용.** Mixamo 작업이 끝날 때까지 보관

### 4-5. 업로드용 머티리얼 ★ 핵심

**메탈릭·러프니스를 연결하지 않은** 새 머티리얼을 만들어 메시에 할당합니다.

```
Image Texture (BaseColor 2K)  ──→  Principled BSDF: Base Color
Image Texture (Normal 2K, Non-Color) → Normal Map → Principled BSDF: Normal
Metallic   = 0.0  (값으로 고정, 텍스처 연결 없음)
Roughness  = 0.6  (값으로 고정, 텍스처 연결 없음)
```

두 가지 버전을 만들어 두면 한쪽이 실패할 때 바로 대체할 수 있습니다.

| 버전 | 연결 | Sister 크기 |
|---|---|---|
| A | BaseColor + Normal | 10.60 MB |
| B | BaseColor만 | 7.70 MB |

> 메탈릭·러프니스는 Mixamo에서 미리보기용일 뿐 리깅 결과에 영향을 주지 않습니다. 언리얼용 FBX를 만들 때 4K 원본 4장을 다시 연결합니다.

### 4-6. 익스포트 설정

| 항목 | 값 |
|---|---|
| Limit to | Selected Objects |
| Object Types | **Mesh 만** |
| Apply Scalings | FBX Scale None |
| Forward / Up | -Z / Y |
| Smoothing | **Face** (스무딩 그룹 포함) |
| Apply Modifiers | 체크 |
| Add Leaf Bones | 해제 |
| Bake Animation | 해제 |
| Path Mode | **Copy** |
| Embed Textures | **체크** (켜지 않으면 Mixamo에서 회색으로 표시) |

```python
bpy.ops.export_scene.fbx(
    filepath=path, use_selection=True, object_types={'MESH'},
    apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y',
    mesh_smooth_type='FACE', use_mesh_modifiers=True,
    add_leaf_bones=False, bake_anim=False,
    path_mode='COPY', embed_textures=True)
```

### 4-7. 익스포트 후 검증

FBX를 다시 임포트하거나 바이너리를 파싱해 확인합니다.

- [ ] `LimbNode = 0`, `Deformer = 0` (뼈대 없음)
- [ ] `Geometry > 0`, `LayerElementSmoothing > 0`
- [ ] 임베드 PNG 개수가 연결한 텍스처 수와 일치 (A: 2장 / B: 1장)
- [ ] 임베드 PNG 해상도 2048
- [ ] 발밑 Z = 0, 키·폭이 원본과 동일
- [ ] 트랜스폼 rot (0,0,0) / scale (1,1,1)

---

## 5. 작업 중 발생한 함정

### 5-1. 2K로 줄였는데 파일이 줄지 않음 (35.6 MB)

- **원인:** 팩(임베드) 상태인 4K 이미지를 `image.copy()` → `scale(2048)` 하면 **복사본이 원본 4K 팩 데이터를 그대로 들고 있어서**, 익스포트 시 4K가 임베드됨
- **해결:** 축소본을 디스크에 저장한 뒤 `image.unpack(method='REMOVE')` → `filepath` 재지정 → `reload()`
- **확인:** FBX 안의 PNG 헤더(IHDR)에서 해상도를 직접 읽어 2048인지 검증

### 5-2. 업로드용 텍스처 폴더를 지운 뒤 재익스포트하면 텍스처가 빠짐

- **증상:** 익스포트 로그에 `embedding file ... failed (No such file or directory)`, 결과 FBX 1.83 MB, 임베드 PNG 0장
- **원인:** `Mixamo_Upload/Textures/`를 지운 상태에서 다시 익스포트
- **해결:** 4K 원본에서 2K를 다시 생성해 링크 후 재익스포트
- **교훈:** 업로드용 텍스처 폴더는 **Mixamo 리깅이 끝날 때까지 지우지 않는다**

---

## 6. 폴더 구조

```
Art/Source/<Character>/
    <Character>_TPose.fbx               Hunyuan3D 원본 (보관)
    Textures/                           4K 원본, 언리얼용 (보관 필수)
        <Character>_BaseColor.png
        <Character>_Normal.png
        <Character>_Metallic.png
        <Character>_Roughness.png
    Mixamo_Upload/
        <Character>_Mixamo_Upload_A.fbx BaseColor + Normal
        <Character>_Mixamo_Upload_B.fbx BaseColor만
        Textures/                       2K 축소본 (리깅 끝나면 삭제 가능)
    Anim/
        Mixamo/                         Mixamo에서 받은 원본
```

---

## 7. Mixamo 업로드 시 주의

- **A 버전부터 업로드**, 실패하면 B 버전
- 마커 배치 화면에서 턱·손목·팔꿈치·무릎·사타구니를 찍을 때, **소매 끝 끈이나 장식이 늘어진 캐릭터는 끈이 아닌 실제 손목에** 찍을 것
- **T포즈와 애니메이션은 같은 업로드 세션에서 다운로드** — 다른 세션에서 받으면 Mixamo가 비율을 개별 조정해 언리얼에서 팔이 꼬임 (Brother 사례)
- 파일명에 공백 금지

---

## 8. 그래도 실패할 때

커뮤니티에서 보고된 추가 해결책입니다. 이 프로젝트에서는 검증하지 않았습니다.

1. **새 Blender 파일로 옮겨 내보내기** — 빈 Blender를 하나 더 열고 메시만 복사·붙여넣기 후 익스포트. 원래 `.blend`에 남은 무언가가 원인인 경우
2. **텍스처 없이 업로드** — 머티리얼을 비우고 메시만 올려 원인이 텍스처인지 메시인지 분리
3. **데시메이트를 단계적으로** — 한 번에 50% 이상 줄이지 말고 여러 번에 나눠 적용
4. **OBJ로 업로드** — 업로드는 되지만 텍스처가 없어 Mixamo에서 회색으로 표시됨. 리깅 목적만이라면 사용 가능

---

## 9. 요약

| 순서 | 작업 | 이유 |
|---|---|---|
| 1 | 트랜스폼 적용 | Hunyuan 출력의 X 90° 회전 제거 |
| 2 | 데시메이트 100만 → 5만 삼각형 | 용량·처리 부담 감소 |
| 3 | 텍스처를 캐릭터 이름으로 추출 | 캐릭터 간 이름 충돌 방지, 언리얼용 원본 확보 |
| 4 | **메탈릭·러프니스 연결 제거** | **"unable to map your existing skeleton" 오류의 직접 원인** |
| 5 | 2K 텍스처 임베드, Face 스무딩 | 용량 감소, 회색 표시 방지, 스무딩 경고 제거 |
| 6 | 익스포트 후 파싱 검증 | 뼈대 0, 임베드 텍스처 수·해상도 확인 |

**결과:** 93.79 MB → 10.60 MB (A) / 7.70 MB (B), 업로드 성공
