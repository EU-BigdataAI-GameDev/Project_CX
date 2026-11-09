# 언리얼 작업 요청: 지원형 드론(SupportDrone) 임포트 + 감정 표정 시스템

아래 내용을 그대로 언리얼 작업용 Claude에게 전달하세요.

---

## 요청

Project_CX(UE 5.6, Interchange 임포트)에 지원형 드론 스태틱 메시를 임포트하고, 정면 스크린에 픽셀 표정을 띄우는 감정 시스템을 만들어 주세요. 블렌더에서 표정 전환을 노드와 드라이버로 구현해 뒀지만 FBX로는 넘어오지 않으므로, 아래 사양대로 언리얼 머티리얼과 블루프린트로 다시 구성해야 합니다.

## 1. 소스 파일

| 파일 | 경로 |
|---|---|
| 메시 | `C:/Users/KyungJun/source/repos/Project_CX/Art/Source/Drone/SM_SupportDrone.fbx` |
| 표정 아틀라스 | `C:/Users/KyungJun/source/repos/Project_CX/Art/Source/Drone/Textures/T_Drone_FaceAtlas.png` |

메시 정보:
- 스태틱 메시 1개입니다. 뼈대와 애니메이션은 없습니다.
- 약 5.4만 삼각형, 크기 약 67 × 44 × 54 cm입니다.
- 피벗은 몸통 중심입니다.
- 정면(스크린)은 언리얼 +X를 향하도록 내보냈습니다. 임포트 후 실제 방향을 확인해 주세요.

FBX 안의 소켓:
- `SOCKET_Thruster`: 바닥 추진기 노즐 입구. 분사 이펙트를 붙일 위치입니다.
- `SOCKET_Screen`: 정면 스크린 앞. 말풍선이나 표정 관련 이펙트용입니다.

## 2. 임포트 규칙

- 대상 폴더: `Content/Project_CX/Characters/Drone/`. 원본 FBX는 Content 폴더 바깥에 두고 임포트만 합니다.
- 임포트 전에 반드시 **"파이프라인 디폴트 사용"**을 누르세요. 이전에 애니메이션을 임포트할 때 쓴 설정이 남아 있으면 임포트가 아무 메시지 없이 실패합니다.
- 스태틱 메시로 임포트하고, 균등 스케일 오프셋은 **1.0**으로 둡니다. FBX 안에 이미 cm 단위로 정상 반영되어 있습니다.
- 머티리얼과 텍스처는 임포트하지 말고 새로 만듭니다. 블렌더 머티리얼은 그대로 옮겨지지 않습니다.
- 소켓 2개가 생성됐는지 스태틱 메시 에디터에서 확인해 주세요.

## 3. 머티리얼 슬롯 (5개, 이름 그대로 매칭)

| 슬롯 | 용도 | 설정값 (블렌더 기준) |
|---|---|---|
| `M_Drone_Shell` | 흰 외장 | BaseColor (0.82, 0.83, 0.85), Roughness 0.32, 클리어코트 약하게 |
| `M_Drone_DarkMetal` | 이음선, 테두리, 캡 | BaseColor (0.035, 0.037, 0.04), Metallic 0.6, Roughness 0.35 |
| `M_Drone_Metal` | 허브, 센서 렌즈, 노즐 | BaseColor (0.18, 0.19, 0.21), Metallic 0.8, Roughness 0.3 |
| `M_Drone_Screen` | 정면 표정 스크린 | **아래 4번 사양** |
| `M_Drone_EmotionGlow` | 정수리 LED, 사이드 LED 링, 추진기 코어·링 | **아래 5번 사양** |

## 4. 표정 아틀라스와 스크린 머티리얼

### 아틀라스 구성

`T_Drone_FaceAtlas.png`는 512 × 256 크기이고, 4열 × 2행 = 8칸으로 나뉩니다. 각 칸은 16 × 16 픽셀 표정을 8배로 확대한 것입니다.

PNG를 열었을 때 보이는 그대로 기준입니다.

| 열 (왼쪽부터) | Emotion 값 | 감정 | 색 (선형 RGB) | 아래 행: A 프레임 | 위 행: B 프레임 |
|---|---|---|---|---|---|
| 0 | 0 | 기본 | 하늘색 (0.35, 0.85, 1.0) | 동그란 눈 + 짧은 입 | 눈 깜빡임 |
| 1 | 1 | 기쁨 | 초록 (0.08, 1.0, 0.18) | ^ ^ 눈 + 웃는 입 | 한 칸 위로 튐 |
| 2 | 2 | 슬픔 | 파랑 (0.08, 0.3, 1.0) | 처진 눈썹 + 눈물 + 처진 입 | 눈물이 한 칸 아래로 흐름 |
| 3 | 3 | 화남 | 빨강 (1.0, 0.05, 0.03) | 치켜뜬 눈 + 이를 악문 입 | 이 가는 동작 + 눈썹 변화 |

- 표정 색은 텍스처에 이미 칠해져 있습니다. 슬픔의 눈물만 연한 하늘색 (0.45, 0.8, 1.0)입니다.
- 배경은 검정입니다.

### 텍스처 설정

- Filter: **Nearest**. 픽셀이 뭉개지지 않게 하기 위함입니다.
- Mip Gen: NoMipmaps
- Compression: UserInterface2D 또는 BC7
- sRGB: 켜기

### 스크린 UV

- `M_Drone_Screen` 슬롯 면은 UV0이 스크린 원 전체를 0~1 정사각형으로 덮도록 펴져 있습니다.
- 다른 슬롯의 UV는 의미가 없습니다.

### 스크린 머티리얼 계산식 (`M_Drone_Screen`, Opaque / DefaultLit)

파라미터:
- `Emotion` (Scalar, 0~3, 기본 0)
- `ScreenBrightness` (Scalar, 기본값은 언리얼 노출 기준으로 조정. 블렌더에서는 3)
- `FaceFlipSeconds` (Scalar, 0.5)

계산:
```
ScreenUV  = TexCoord[0]
FrameB    = floor(Time / FaceFlipSeconds) mod 2          // 0 = A, 1 = B
AtlasUV.x = ScreenUV.x * 0.25 + floor(Emotion) * 0.25
AtlasUV.y = ScreenUV.y * 0.5  + (1 - FrameB) * 0.5       // 언리얼은 V=0이 위쪽, A 프레임은 아래 행
FaceColor = Texture(T_Drone_FaceAtlas, AtlasUV)

// LED 점 마스크: 각 픽셀을 동그란 점으로
Cell      = frac(ScreenUV * 16) - 0.5
LEDMask   = 1 - smoothstep(0.30, 0.46, length(Cell))

BaseColor = (0.006, 0.007, 0.009)
Roughness = 0.12
Emissive  = FaceColor * LEDMask * ScreenBrightness
```

검증:
- 표정이 위아래로 뒤집혀 보이면 `(1 - FrameB)` 부분을 `FrameB`로 바꾸고, V를 `1 - ScreenUV.y`로 시도해 주세요.
- 좌우가 뒤집혀 보이면 `ScreenUV.x`를 `1 - ScreenUV.x`로 바꿔 주세요.
- 기쁨 표정(Emotion = 1)으로 확인하는 게 가장 쉽습니다. 입꼬리가 올라가 있고 눈이 ^ ^ 모양이면 정상입니다.

## 5. 감정 연동 발광 (`M_Drone_EmotionGlow`)

- 파라미터 `Emotion`(Scalar)을 받아, 위 표의 색 4개 중 하나를 고릅니다. 보간 없이 딱 끊어서 선택합니다.
- 블렌더에서는 Emission 강도 8로 썼습니다. 언리얼에서는 블룸이 보이도록 적당히 조정해 주세요.
- 이 머티리얼은 정수리 LED, 양쪽 사이드 LED 링, 추진기 코어, 추진기 입구 링에 공통으로 쓰입니다.

## 6. 블루프린트 `BP_SupportDrone`

1. **Enum** `E_DroneEmotion`: Neutral = 0, Happy = 1, Sad = 2, Angry = 3
2. **컴포넌트**: StaticMesh(`SM_SupportDrone`), Niagara(`SOCKET_Thruster`에 부착, 아래 방향)
3. **BeginPlay**: Screen 슬롯과 EmotionGlow 슬롯에 각각 Dynamic Material Instance를 만들어 변수로 저장합니다.
4. **함수 `SetEmotion(E_DroneEmotion NewEmotion)`**:
   - 두 다이나믹 머티리얼의 `Emotion` 파라미터를 같은 값으로 설정합니다.
   - 추진기 Niagara의 색 파라미터도 같은 감정 색으로 설정합니다.
   - 하늘색·초록·파랑·빨강 4색은 배열이나 맵 변수로 한 곳에서 관리해 주세요.
5. **에디터 확인용**: `Emotion` 변수를 Expose on Spawn + Instance Editable로 두고, Construction Script에서 바로 반영되게 해 주세요.
6. **호버 대기 동작**: 메시 컴포넌트를 Z축으로 ±3 cm, 주기 2초 정도 사인파로 오르내리고, 약간 기울어지게 해 주세요. 뼈대가 없으므로 컴포넌트 트랜스폼으로 처리합니다.

## 7. 추진기 이펙트

- `SOCKET_Thruster`에서 아래로 뿜어지는 반투명 빛 기둥 + 작은 입자로 만들어 주세요.
- 길이 약 35 cm, 아래로 갈수록 넓어지며 사라지게 합니다.
- 색은 `SetEmotion`에서 넘겨주는 감정 색을 따릅니다.
- 참고: 블렌더에는 같은 형태의 미리보기 메시(`Drone_ThrusterBeam_PREVIEW`)가 있지만 FBX에는 넣지 않았습니다.

## 8. 완료 기준

- [ ] 드론이 실제 크기(약 67 × 44 × 54 cm)로 임포트되고, 정면 스크린이 +X를 향함
- [ ] 소켓 2개 존재
- [ ] Emotion 0~3을 바꿀 때 스크린 표정과 색이 표에 맞게 바뀌고, 0.5초마다 A/B 프레임이 번갈아 보임
- [ ] 표정이 위아래·좌우로 뒤집히지 않음
- [ ] 정수리 LED, 사이드 링, 추진기 코어·링, 분사 이펙트가 같은 감정 색으로 바뀜
- [ ] 픽셀이 Nearest 필터로 또렷하게 보이고, 동그란 LED 점 형태로 표시됨
- [ ] 레벨에서 호버 대기 동작이 재생됨

작업이 끝나면 Emotion 0~3 각각의 스크린 캡처와 만든 애셋 목록을 알려주세요.
