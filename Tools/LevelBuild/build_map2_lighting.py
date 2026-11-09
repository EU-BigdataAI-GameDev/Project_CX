"""
Map2 조명 빌더 — 전역 조명(스카이라이트/달빛/포그/PPV) + 구간별 실제 광원(전구, 형광등, 네온, 경고등).

- 지형 빌더(build_map2.py, 태그 GEN_Map2)와 분리: 이 스크립트는 태그 GEN_Map2_Light 액터만 지우고 다시 만든다.
  → 지형을 재생성해도 조명은 유지되고, 조명만 따로 다시 돌릴 수 있다.
- 좌표는 .claude/Plans/맵2_레벨디자인.md 의 구간 표 기준.
- 네온 스트립은 Map1에서 만든 MI_Neon_* 를 재사용한다. 카메라 앞(X < -120)에는 두지 않는다.
"""
import unreal

LEVEL = "/Game/Project_CX/Maps/Map2"
TAG = "GEN_Map2_Light"
MAT_DIR = "/Game/Project_CX/Environment/Materials"
CUBE = "/Engine/BasicShapes/Cube.Cube"

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if les.get_current_level().get_outermost().get_name() != LEVEL:
    les.load_level(LEVEL)
for a in eas.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
        eas.destroy_actor(a)

count = {}
def _finish(a, folder, label, kind):
    a.set_folder_path(folder)
    a.set_actor_label(label)
    a.tags = [TAG]
    count[kind] = count.get(kind, 0) + 1
    return a

def _color(rgb):
    return unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255)

WARM = (1.0, 0.62, 0.3)
BULB = (1.0, 0.78, 0.5)
FLUO = (0.75, 0.9, 1.0)
COLD = (0.45, 0.6, 1.0)
CYAN = (0.1, 0.85, 1.0)
PINK = (1.0, 0.2, 0.6)
PURPLE = (0.6, 0.3, 1.0)
RED = (1.0, 0.08, 0.05)
GREEN = (0.3, 1.0, 0.45)

def point(label, x, y, z, rgb, cd=40.0, radius=800.0, shadows=False, folder="Lighting/Practical"):
    a = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z))
    c = a.point_light_component
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", cd)
    c.set_editor_property("light_color", _color(rgb))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("cast_shadows", shadows)
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    return _finish(a, folder, "PL_" + label, "point")

def spot(label, x, y, z, pitch, yaw, rgb, cd=300.0, radius=2500.0, outer=25.0, inner=10.0, shadows=True, folder="Lighting/Practical"):
    a = eas.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(x, y, z), unreal.Rotator(pitch=pitch, yaw=yaw, roll=0))
    c = a.spot_light_component
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", cd)
    c.set_editor_property("light_color", _color(rgb))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("outer_cone_angle", outer)
    c.set_editor_property("inner_cone_angle", inner)
    c.set_editor_property("cast_shadows", shadows)
    c.set_editor_property("volumetric_scattering_intensity", 3.0)
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    return _finish(a, folder, "SL_" + label, "spot")

def neon(label, cx, cy, cz, length, color, axis="Y", thick=6, folder="Lighting/Neon"):
    half_x = (length if axis == "X" else thick) / 2
    assert cx - half_x >= -120, label   # 카메라 앞 금지
    a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector(cx, cy, cz))
    s = [thick / 100] * 3
    s["XYZ".index(axis)] = length / 100
    a.set_actor_scale3d(unreal.Vector(*s))
    comp = a.static_mesh_component
    comp.set_material(0, unreal.load_asset(f"{MAT_DIR}/MI_Neon_{color}"))
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("cast_shadow", False)
    return _finish(a, folder, "NEON_" + label, "neon")

# ================================================================ 전역: 맵 전체를 비추는 기본광 + 대기
ATM = "Lighting/Atmosphere"
sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 6700, 3000))
sc = sky.light_component
sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
sc.set_editor_property("intensity", 1.2)
sc.set_editor_property("light_color", _color((0.45, 0.55, 0.8)))
cube = unreal.load_asset("/Engine/MapTemplates/Sky/SunsetAmbientCubemap")
if cube:   # 하늘이 없는 지하라 씬 캡처 대신 고정 큐브맵으로 균일한 앰비언트를 준다
    sc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sc.set_editor_property("cubemap", cube)
print("skylight cubemap:", bool(cube))
_finish(sky, ATM, "SkyLight_Cave", "sky")

# 동굴 천장 틈으로 새는 차가운 빛: 카메라 쪽 위에서 비춰 캐릭터/발판 앞면을 드러낸다
key = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 6700, 3500), unreal.Rotator(pitch=-48, yaw=25, roll=0))
kc = key.light_component
kc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
kc.set_editor_property("intensity", 2.0)
kc.set_editor_property("light_color", _color((0.55, 0.65, 1.0)))
kc.set_editor_property("cast_shadows", True)
_finish(key, ATM, "DirLight_CaveShaft", "dir")

fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 6700, -300))
fc = fog.component
fc.set_editor_property("fog_density", 0.02)
fc.set_editor_property("fog_height_falloff", 0.06)
fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.02, 0.03, 0.055, 1.0))
fc.set_editor_property("start_distance", 1200.0)   # 플레이 레인(카메라 500~800 거리)은 선명하게
fc.set_editor_property("enable_volumetric_fog", True)
fc.set_editor_property("volumetric_fog_extinction_scale", 1.0)
_finish(fog, ATM, "Fog_Underground", "fog")

ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 6700, 0))
ppv.set_editor_property("unbound", True)
s = ppv.settings
for k, v in [("auto_exposure_min_brightness", -0.5), ("auto_exposure_max_brightness", 1.0), ("auto_exposure_bias", 0.6),
             ("bloom_intensity", 0.9), ("vignette_intensity", 0.45)]:
    s.set_editor_property("override_" + k, True)
    s.set_editor_property(k, v)
s.set_editor_property("override_color_saturation", True)
s.set_editor_property("color_saturation", unreal.Vector4(1.0, 1.05, 1.15, 1.0))
ppv.set_editor_property("settings", s)
_finish(ppv, ATM, "PPV_Global", "ppv")

# ================================================================ S0/S1 침수 수로: 따뜻한 수상 가옥 창문 + 청록 수면 반사
L = "Lighting/S1_FloodedCanal"
point("S0_Entrance", 60, -500, 320, WARM, cd=60.0, radius=900.0, shadows=True, folder=L)
neon("S0_Sign", 170, -300, 230, 180, "Orange")
for i, y in enumerate((800, 1250, 1700, 2150)):
    point(f"S1_House_{i}", 260, y, 260, WARM, cd=45.0, radius=750.0, folder=L)
for i, (y, top) in enumerate([(910, 0), (1300, 50), (1690, 0), (2110, 40)]):   # 발판마다 매달린 전구(발판 가독성)
    point(f"S1_PalletBulb_{i}", 60, y, top + 230, BULB, cd=30.0, radius=450.0, shadows=True, folder=L)
for i, y in enumerate((900, 1600, 2250)):
    point(f"S1_WaterGlow_{i}", 0, y, -90, CYAN, cd=12.0, radius=700.0, folder=L)
neon("S1_CanalEdge", 158, 1500, -2, 1800, "Cyan", thick=4)
point("S1_ExitSteps", 60, 2320, 120, BULB, cd=35.0, radius=600.0, folder=L)

# ================================================================ S2 갈라짐: 여동생 통로는 형광등, 오빠 지붕은 보라 림라이트
L = "Lighting/S2_Split"
for i, y in enumerate(range(3200, 5600, 480)):
    point(f"S2_DuctFluo_{i}", -60, y, 150, FLUO, cd=18.0, radius=420.0, folder=L)
neon("S2_DuctGuide", -112, 4300, 165, 2560, "Cyan", thick=4)   # 처마 아래 앞 모서리: 여동생 길 안내선
neon("S2_ForkArrow", -15, 2900, 60, 140, "Cyan", thick=5)
point("S2_Fork", 40, 2750, 400, WARM, cd=50.0, radius=900.0, shadows=True, folder=L)
spot("S2_RoofRim", 400, 4150, 1300, -55, 180, PURPLE, cd=400.0, radius=2400.0, outer=40.0, inner=20.0, folder=L)
for i, (y, top) in enumerate([(3350, 260), (4130, 310), (4500, 365), (5040, 300)]):
    point(f"S2_RoofLamp_{i}", 120, y, top + 260, BULB, cd=35.0, radius=650.0, shadows=(i % 2 == 0), folder=L)
neon("S2_RoofSign", 160, 4150, 620, 300, "Pink")
point("S2_Merge", 40, 5800, 380, WARM, cd=55.0, radius=900.0, shadows=True, folder=L)

# ================================================================ S3 단면 공동주택: 층마다 천장 전구
L = "Lighting/S3_Tenement"
for i, y in enumerate((6300, 7250, 8200, 9050)):
    point(f"S3_F1_Bulb_{i}", 40, y, 300, BULB, cd=40.0, radius=700.0, shadows=True, folder=L)
for i, y in enumerate((7780, 8300, 8850)):
    point(f"S3_F2_Bulb_{i}", 40, y, 364 + 280, BULB, cd=40.0, radius=700.0, shadows=True, folder=L)
for i, y in enumerate((9100, 9330)):
    point(f"S3_F3_Bulb_{i}", 40, y, 728 + 300, BULB, cd=40.0, radius=650.0, shadows=True, folder=L)
point("S3_StairA", 110, 7320, 250, WARM, cd=25.0, radius=500.0, folder=L)
point("S3_StairB", 110, 8720, 364 + 250, WARM, cd=25.0, radius=500.0, folder=L)
# 2층 바닥 구멍 가장자리 경고선
for x_len, y in ((260, 7895), (260, 8055)):
    neon(f"S3_F2_HoleLine_{y}", 15, y, 366, x_len, "Red", axis="X", thick=3)
neon("S3_ExitDoor", 20, 9395, 728 + 255, 260, "Cyan", axis="X", thick=6)
point("S3_Outside", 250, 7700, 1300, COLD, cd=60.0, radius=1600.0, folder=L)

# ================================================================ S4 옥상 빨래 시장: 줄전구 + 노점 네온 + 저격 위치 경고등
L = "Lighting/S4_RooftopMarket"
TOP = 728.0
for i, y in enumerate(range(9600, 11400, 380)):
    point(f"S4_StringBulb_{i}", 30, y, TOP + 320, BULB, cd=22.0, radius=520.0, shadows=(i % 3 == 0), folder=L)
for i, (y, c) in enumerate([(9700, "Pink"), (10250, "Cyan"), (10800, "Purple"), (11250, "Orange")]):
    neon(f"S4_Stall_{i}", 175, y, TOP + 230, 320, c, thick=5)
neon("S4_Stage", 22, 10100, TOP + 52, 400, "Pink", thick=4)
point("S4_StageGlow", 60, 10100, TOP + 200, PINK, cd=25.0, radius=600.0, folder=L)
# 원거리 로봇 첫 등장: 불시타 방지를 위해 물탱크 위를 붉게 강조 + 레인 쪽으로 경고 스포트
point("S4_Sniper_Warn", 520, 11150, TOP + 620, RED, cd=90.0, radius=1200.0, folder=L)
spot("S4_Sniper_Search", 480, 11150, TOP + 650, -30, 200, RED, cd=350.0, radius=2000.0, outer=16.0, inner=6.0, folder=L)
point("S4_EdgeFill", 100, 10450, TOP + 450, COLD, cd=40.0, radius=1400.0, folder=L)

# ================================================================ S5 암반 하강 + 출구 게이트
L = "Lighting/S5_CaveDescent"
point("S5_ShackRoof", 60, 11650, 540 + 280, WARM, cd=40.0, radius=700.0, shadows=True, folder=L)
point("S5_Stair", 60, 12120, 700, BULB, cd=30.0, radius=650.0, folder=L)
for i, (y, top) in enumerate([(12550, 200), (13100, 110)]):
    point(f"S5_LedgeGlow_{i}", 80, y, top + 220, GREEN, cd=18.0, radius=550.0, folder=L)   # 동굴 발광 이끼 톤
point("S5_CaveBounce", 600, 12800, 500, COLD, cd=50.0, radius=1800.0, folder=L)
Y_END = 14200.0
neon("S5_Gate_Top", 15, Y_END - 45, 262, 250, "Cyan", axis="X", thick=10)
for x in (-60, 110):
    neon(f"S5_Gate_Side_{x}", x, Y_END - 45, 130, 260, "Cyan", axis="Z", thick=10)
point("S5_GateGlow", 20, Y_END - 200, 220, CYAN, cd=120.0, radius=1400.0, shadows=True, folder=L)
for i, y in enumerate(range(13450, 14100, 200)):
    neon(f"S5_Guide_{i}", -60, y, 3, 110, "Cyan", thick=4)

les.save_current_level()
print("lighting built", count)
