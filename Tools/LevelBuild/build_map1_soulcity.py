"""
Map1_SoulCity 빌더 — Docs/Map1_레벨디자인_기획안.md 의 5구간 구조를 Soul City 에셋으로 구현한다.

좌표계(프로젝트 이동/카메라 기준)
  +Y : 진행 방향(화면 오른쪽)     X : 깊이(카메라는 -X 쪽에서 +X를 바라봄)     Z : 위
플레이 레인 : X ∈ [-250, 250]  (앞/뒤에 투명 경계벽)
플레이어 실측 : 캡슐 r34 / hh88(오빠), hh80(여동생), 속도 450, JumpZ 420, 중력 980 → 최고 점프 90cm

※ 이 스크립트는 태그 GEN_Map1SoulCity 가 붙은 액터만 지우고 다시 만든다.
   사람이 이 레벨을 손으로 수정한 뒤에는 재실행하지 말 것(수정 내용이 사라진다).
"""
import unreal, json, math, random

LEVEL = "/Game/Project_CX/Maps/Map1_SoulCity"
TAG = "GEN_Map1SoulCity"
MAT_DIR = "/Game/Project_CX/Environment/Materials"
MANIFEST = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map1_SoulCity_manifest.json")

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

INV = {}
for m in json.load(open(unreal.Paths.project_saved_dir() + "soul_inventory.json")):
    INV[m["path"].split("/")[-1]] = m
_mesh_cache = {}
def mesh(name):
    if name not in _mesh_cache:
        path = INV[name]["path"] if name in INV else name
        _mesh_cache[name] = unreal.load_asset(path)
        if _mesh_cache[name] is None:
            raise RuntimeError("mesh not found: " + name)
    return _mesh_cache[name]

CUBE = "/Engine/BasicShapes/Cube.Cube"

for bp_path in ["/Game/Project_CX/Characters/Brother/BP_Brother", "/Game/Project_CX/Characters/Sister/BP_Sister"]:
    cdo = unreal.get_default_object(unreal.load_asset(bp_path).generated_class())
    cm = cdo.get_editor_property("character_movement")
    cap = cdo.get_editor_property("capsule_component")
    print(bp_path.split("/")[-1], "gravity_scale", cm.get_editor_property("gravity_scale"), "jumpZ", cm.get_editor_property("jump_z_velocity"),
          "walk", cm.get_editor_property("max_walk_speed"), "step", cm.get_editor_property("max_step_height"),
          "capsule", cap.get_editor_property("capsule_radius"), cap.get_editor_property("capsule_half_height"))
rng = random.Random(7)
manifest = []

# ---------------------------------------------------------------- level
if eal.does_asset_exist(LEVEL):
    if les.get_current_level().get_outermost().get_name() != LEVEL:
        les.load_level(LEVEL)
else:
    les.new_level(LEVEL)

for a in eas.get_all_level_actors():
    folder = str(a.get_folder_path())
    if TAG in [str(t) for t in a.tags] or folder.startswith("_KitTest") or folder.startswith("_Lineup"):
        eas.destroy_actor(a)

def _finish(a, folder, label):
    a.set_folder_path(folder)
    if label:
        a.set_actor_label(label)
    a.tags = [TAG]
    return a

def _record(a, kind, extra=None):
    l, r, s = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
    e = {"label": a.get_actor_label(), "kind": kind, "folder": str(a.get_folder_path()),
         "loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)], "rot": [round(r.pitch, 1), round(r.yaw, 1), round(r.roll, 1)],
         "scale": [round(s.x, 3), round(s.y, 3), round(s.z, 3)]}
    if extra:
        e.update(extra)
    manifest.append(e)

# ---------------------------------------------------------------- materials (neon)
def ensure_neon_material():
    path = MAT_DIR + "/M_NeonEmissive"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = asset_tools.create_asset("M_NeonEmissive", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    c = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    c.set_editor_property("parameter_name", "Color")
    c.set_editor_property("default_value", unreal.LinearColor(0.0, 0.8, 1.0, 1.0))
    i = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 200)
    i.set_editor_property("parameter_name", "Intensity")
    i.set_editor_property("default_value", 20.0)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 80)
    mel.connect_material_expressions(c, "", mul, "A")
    mel.connect_material_expressions(i, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat

NEON_COLORS = {
    "Cyan": (0.0, 0.85, 1.0), "Pink": (1.0, 0.1, 0.55), "Orange": (1.0, 0.45, 0.05),
    "Green": (0.15, 1.0, 0.3), "Red": (1.0, 0.03, 0.02), "Blue": (0.1, 0.3, 1.0), "Purple": (0.55, 0.1, 1.0),
}
_neon_mi = {}
def neon_mi(color, intensity=25.0):
    key = (color, intensity)
    if key in _neon_mi:
        return _neon_mi[key]
    name = f"MI_Neon_{color}" + ("" if intensity == 25.0 else f"_{int(intensity)}")
    path = MAT_DIR + "/" + name
    if eal.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = asset_tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, ensure_neon_material())
    r, g, b = NEON_COLORS[color]
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(r, g, b, 1.0))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Intensity", intensity)
    eal.save_loaded_asset(mi)
    _neon_mi[key] = mi
    return mi

# ---------------------------------------------------------------- placement helpers
def art(name, cx, cy, bottom_z, yaw=90.0, scale=1.0, folder="Art", label=None, pitch=0.0, roll=0.0, pivot=False):
    """메시 바운드 중심을 (cx, cy)에, 바닥을 bottom_z에 맞춰 배치(pivot=True면 피벗 그대로). 충돌 없음."""
    m = INV[name]
    sc = scale if isinstance(scale, (tuple, list)) else (scale, scale, scale)
    if pivot:
        loc = unreal.Vector(cx, cy, bottom_z)
    else:
        lcx = (m["min"][0] + m["max"][0]) / 2 * sc[0]
        lcy = (m["min"][1] + m["max"][1]) / 2 * sc[1]
        rad = math.radians(yaw)
        wx = lcx * math.cos(rad) - lcy * math.sin(rad)
        wy = lcx * math.sin(rad) + lcy * math.cos(rad)
        loc = unreal.Vector(cx - wx, cy - wy, bottom_z - m["min"][2] * sc[2])
    a = eas.spawn_actor_from_object(mesh(name), loc, unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    a.set_actor_scale3d(unreal.Vector(*sc))
    comp = a.static_mesh_component
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    _finish(a, folder, label)
    _record(a, "art", {"mesh": name})
    return a

def span_y(name, yaw=90.0, scale=1.0):
    s = INV[name]["size"]
    return (s[0] if yaw % 180 == 90 else s[1]) * scale

def span_x(name, yaw=90.0, scale=1.0):
    s = INV[name]["size"]
    return (s[1] if yaw % 180 == 90 else s[0]) * scale

def col(label, x0, x1, y0, y1, z0, z1, folder="Collision"):
    """보이지 않는 충돌 박스(플레이 가능한 지형)."""
    a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    a.set_actor_scale3d(unreal.Vector((x1 - x0) / 100, (y1 - y0) / 100, (z1 - z0) / 100))
    comp = a.static_mesh_component
    comp.set_collision_profile_name("BlockAll")
    # 스프링암 충돌 테스트가 투명벽/지형에 걸려 카메라가 당겨지지 않도록
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_editor_property("visible", False)
    comp.set_editor_property("hidden_in_game", True)
    comp.set_editor_property("cast_shadow", False)
    _finish(a, folder, "COL_" + label)
    _record(a, "collision", {"box": [x0, x1, y0, y1, z0, z1]})
    return a

def ramp(label, x0, x1, y0, z0, y1, z1, thick=20, folder="Collision"):
    """계단용 경사 충돌(윗면이 (y0,z0)→(y1,z1))."""
    dy, dz = y1 - y0, z1 - z0
    length = math.hypot(dy, dz)
    ang = math.degrees(math.atan2(dz, dy))
    ny, nz = -dz / length, dy / length
    cy = (y0 + y1) / 2 - ny * thick / 2
    cz = (z0 + z1) / 2 - nz * thick / 2
    a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((x0 + x1) / 2, cy, cz),
                                    unreal.Rotator(pitch=0, yaw=90, roll=0))
    # 큐브를 yaw 90으로 돌린 뒤 로컬 X(=월드 Y) 방향으로 경사를 준다
    a.set_actor_rotation(unreal.Rotator(pitch=ang, yaw=90, roll=0), False)
    a.set_actor_scale3d(unreal.Vector(length / 100, (x1 - x0) / 100, thick / 100))
    comp = a.static_mesh_component
    comp.set_collision_profile_name("BlockAll")
    # 스프링암 충돌 테스트가 투명벽/지형에 걸려 카메라가 당겨지지 않도록
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_editor_property("visible", False)
    comp.set_editor_property("hidden_in_game", True)
    _finish(a, folder, "COL_" + label)
    _record(a, "collision_ramp", {"from": [y0, z0], "to": [y1, z1], "angle": round(ang, 1)})
    return a

def neon(label, cx, cy, cz, length, color, axis="Y", thick=6, folder="Lighting/Neon", intensity=25.0):
    a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector(cx, cy, cz))
    sx, sy, sz = thick / 100, thick / 100, thick / 100
    if axis == "Y": sy = length / 100
    elif axis == "Z": sz = length / 100
    else: sx = length / 100
    a.set_actor_scale3d(unreal.Vector(sx, sy, sz))
    comp = a.static_mesh_component
    comp.set_material(0, neon_mi(color, intensity))
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("cast_shadow", False)
    _finish(a, folder, "NEON_" + label)
    _record(a, "neon", {"color": color})
    return a

def _color(rgb):
    return unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255)

def point(label, x, y, z, rgb, cd=40.0, radius=700.0, shadows=False, folder="Lighting"):
    a = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z))
    c = a.point_light_component
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", cd)
    c.set_editor_property("light_color", _color(rgb))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("cast_shadows", shadows)
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    _finish(a, folder, "PL_" + label)
    _record(a, "pointlight", {"cd": cd, "rgb": rgb})
    return a

def spot(label, x, y, z, pitch, yaw, rgb, cd=300.0, radius=2500.0, outer=18.0, inner=8.0, shadows=True, folder="Lighting"):
    a = eas.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(x, y, z), unreal.Rotator(pitch=pitch, yaw=yaw, roll=0))
    c = a.spot_light_component
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", cd)
    c.set_editor_property("light_color", _color(rgb))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("outer_cone_angle", outer)
    c.set_editor_property("inner_cone_angle", inner)
    c.set_editor_property("cast_shadows", shadows)
    c.set_editor_property("volumetric_scattering_intensity", 4.0)
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    _finish(a, folder, "SL_" + label)
    _record(a, "spotlight", {"cd": cd, "rgb": rgb})
    return a

def marker(label, x, y, z, folder="Gameplay/Markers"):
    a = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, z))
    _finish(a, folder, "MARKER_" + label)
    _record(a, "marker")
    return a

WARM = (1.0, 0.62, 0.3)
COLD = (0.55, 0.75, 1.0)

# ---------------------------------------------------------------- reusable dressing
SHACKS = ["SM_Slums_Distance_01a", "SM_Slums_Distance_01b", "SM_Slums_Distance_01c", "SM_Slums_Distance_02b",
          "SM_Slums_Distance_03a_OP", "SM_Slums_Distance_02e", "SM_Slums_Distance_05a_Opt"]
SIGNS = ["SM_Slums_Sign01a", "SM_Slums_Sign02a", "SM_Slums_Sign02b", "SM_Slums_Sign03a", "SM_Slums_Sign04a",
         "SM_Slums_Sign05a", "SM_Slums_Sign06a", "SM_Slums_Sign01b_Bent", "SM_Slums_Sign02d"]
PROPS = ["SM_Slums_Trashcan_Open01", "SM_Slums_Trashcan_Closed02", "SM_Slums_Trashbag", "SM_Slums_Trashcan_Open02",
         "SM_Slums_Trash_02c", "SM_Slums_Pipe_01a", "SM_Slums_Pipe_03a"]

def facade_row(sec, y0, y1, front_x, base_z, tiers=2, neon_chance=0.5, folder_root="Art"):
    """카메라를 향한 판자촌 파사드 한 줄(아래층 + 위로 쌓은 층 + 간판/네온/전선/창문 불빛)."""
    y = y0
    i = 0
    while y < y1 - 200:
        name = rng.choice(SHACKS)
        w = span_y(name)
        if y + w > y1 + 150:
            break
        cy = y + w / 2
        d = span_x(name)
        art(name, front_x + d / 2, cy, base_z, folder=f"{folder_root}/{sec}/Facade")
        top = base_z + INV[name]["size"][2]
        # 윗층: 조금 뒤로 물려 쌓아 실루엣에 깊이를 준다
        for t in range(1, tiers):
            upper = rng.choice(SHACKS)
            uw = span_y(upper)
            art(upper, front_x + span_x(upper) / 2 + 60 * t, cy + rng.uniform(-60, 60), top - 20, folder=f"{folder_root}/{sec}/Facade")
            top += INV[upper]["size"][2] - 20
        # 간판 + 네온
        if rng.random() < 0.8:
            s = rng.choice(SIGNS)
            art(s, front_x - 25, cy + rng.uniform(-w * 0.25, w * 0.25), base_z + rng.uniform(230, 330), folder=f"{folder_root}/{sec}/Signs")
        if rng.random() < neon_chance:
            col_name = rng.choice(["Cyan", "Pink", "Pink", "Purple", "Orange"])
            nz = base_z + rng.uniform(250, 420)
            neon(f"{sec}_{i}", front_x - 12, cy, nz, w * 0.7, col_name, "Y")
            point(f"{sec}_Neon_{i}", front_x - 120, cy, nz, NEON_COLORS[col_name], cd=25.0, radius=650.0)
        # 창문 불빛(따뜻한 실내광)
        if rng.random() < 0.6:
            point(f"{sec}_Win_{i}", front_x - 40, cy + rng.uniform(-80, 80), base_z + 250, WARM, cd=10.0, radius=380.0)
        y += w + rng.uniform(-40, 30)
        i += 1

def wires(sec, y0, y1, x, z, folder="Art"):
    y = y0
    while y < y1:
        art("SM_Slums_Wires_02c_OP", x, y + 430, z + rng.uniform(-40, 60), folder=f"{folder}/{sec}/Wires")
        y += 860

def ground_tiles(sec, y0, y1, top_z, x0=-300, x1=300, folder="Art"):
    """바닥 타일(200x200). 충돌은 col()이 따로 담당."""
    nx = int(round((x1 - x0) / 200))
    y = y0
    while y < y1 - 1:
        for ix in range(nx):
            art("SM_Slums_Floor_02a", x0 + 100 + ix * 200, y + 100, top_z - 18, yaw=0, folder=f"{folder}/{sec}/Ground")
        y += 200

def props_row(sec, y0, y1, x, z, count, folder="Art"):
    for k in range(count):
        p = rng.choice(PROPS)
        art(p, x + rng.uniform(-20, 20), rng.uniform(y0, y1), z, yaw=rng.uniform(0, 360), folder=f"{folder}/{sec}/Props")

LOW_PROPS = ["SM_Slums_Trash_01a", "SM_Slums_Trashbag", "SM_Slums_Trash_02c", "SM_Slums_Pipe_05f", "SM_Slums_Trashcan_Lid",
             "SM_Cave_Rock_Small01", "SM_Slums_Pipe_05c"]

def foreground(sec, y0, y1, skip=None, folder="Art"):
    """카메라와 레인 사이 전경 바닥(걷지 못하는 영역). 플레이어 발밑을 가리지 않게 90cm 이하 소품만 둔다."""
    y = y0
    while y < y1 - 1:
        if not (skip and skip[0] <= y < skip[1]):
            for xx in (-400, -600, -800, -1000):
                art("SM_Slums_Floor_02a", xx, y + 100, -20 - 18, yaw=0, folder=f"{folder}/{sec}/Foreground")
        y += 200
    # 레인 앞 가장자리(낮은 연석 느낌의 슬래브)
    y = y0
    while y < y1 - 1:
        if not (skip and skip[0] <= y < skip[1]):
            art("SM_Slums_Floor_01a", -300, y + 100, -20 - 64 + 20, yaw=90, scale=(1.0, 0.3, 1.0), folder=f"{folder}/{sec}/Foreground")
        y += 260
    for k in range(int((y1 - y0) / 220)):
        yy = rng.uniform(y0, y1)
        if skip and skip[0] - 100 <= yy < skip[1] + 100:
            continue
        art(rng.choice(LOW_PROPS), rng.uniform(-950, -430), yy, -20, yaw=rng.uniform(0, 360), folder=f"{folder}/{sec}/Foreground")

def industrial_wall(sec, y0, y1, front_x, z_bottom, z_top, name="SM_Slums_Wall_03b", folder="Art"):
    """녹슨 산업 패널로 구조물 정면을 [y0,y1]x[z_bottom,z_top]에 정확히 맞춰 덮는다(윗면 위로 솟지 않게)."""
    w, h = span_y(name), INV[name]["size"][2]
    n = max(1, int(math.ceil((y1 - y0) / w - 0.25)))
    sy = (y1 - y0) / (n * w)
    tiers = max(1, int(math.ceil((z_top - z_bottom) / h - 0.25)))
    sz = (z_top - z_bottom) / (tiers * h)
    for i in range(n):
        for t in range(tiers):
            art(name, front_x + span_x(name) / 2, y0 + (i + 0.5) * w * sy, z_bottom + t * h * sz,
                scale=(sy, 1.0, sz), folder=f"{folder}/{sec}/Structure")

# ================================================================ PLAY SPACE
LANE_X0, LANE_X1 = -250, 250
Y_START, Y_END = -700, 12900

# 레인 경계(앞/뒤/양끝) — 2.5D 레인 밖으로 떨어지지 않게
col("Bound_Front", -290, -260, Y_START, Y_END, -200, 3200, "Collision/Bounds")
col("Bound_Back", 260, 290, Y_START, Y_END, -200, 3200, "Collision/Bounds")
col("Bound_Left", -300, 300, Y_START - 40, Y_START, -200, 3200, "Collision/Bounds")
col("Bound_Right", -300, 300, Y_END, Y_END + 40, -200, 3200, "Collision/Bounds")

# ---------------- S1 스크랩 거리 (Y -700 → 3000) : 기본 이동/점프 튜토리얼
S1 = "S1_ScrapStreet"
col("S1_Ground_A", LANE_X0, LANE_X1, Y_START, 2100, -60, 0, f"Collision/{S1}")
col("S1_Deck", LANE_X0, LANE_X1, 900, 1700, 0, 50, f"Collision/{S1}")               # +50 점프 (여유 40)
col("S1_ChannelFloor", LANE_X0, LANE_X1, 2100, 2300, -100, -40, f"Collision/{S1}")  # 빠져도 40 단차라 걸어서 나옴
col("S1_Ground_B", LANE_X0, LANE_X1, 2300, 3000, -60, 0, f"Collision/{S1}")          # 간격 200 (이론 386)

ground_tiles(S1, Y_START, 2100, 0)
ground_tiles(S1, 2300, 3000, 0)
for yy in range(1000, 1700, 200):     # 나무 데크(윗면 z=50)
    for xx in (-130, 130):
        art("SM_Slums_Floor_01a", xx, yy, 50 - 64, yaw=0, folder=f"Art/{S1}/Deck")
# 하수 수로
art("SM_Water_A", 0, 2200, -25, yaw=0, scale=(0.15, 0.19, 1.0), folder=f"Art/{S1}/Channel")
for yy in (2110, 2290):
    for xx in (-200, 0, 200):
        art("SM_Slums_Floor_02a", xx, yy, -40 - 18, yaw=0, scale=(1, 0.1, 1), folder=f"Art/{S1}/Channel")
art("SM_Slums_Pipe_05b", 300, 2200, -40, yaw=90, folder=f"Art/{S1}/Channel")
point("S1_ChannelGlow", 0, 2200, 20, (0.2, 0.9, 0.7), cd=15.0, radius=420)

# 시작 막다른 벽 + 좌측 끝
art("SM_Slums_Wall_03a", 0, Y_START + 20, 0, yaw=0, folder=f"Art/{S1}/Structure")
art("SM_Slums_Wall_03a", 0, Y_START + 20, 629, yaw=0, folder=f"Art/{S1}/Structure")
facade_row(S1, Y_START, 3000, 300, 0, tiers=3)
wires(S1, Y_START, 3000, 150, 620)
props_row(S1, Y_START + 200, 2000, 275, 0, 14)
foreground(S1, Y_START, 3000, skip=(2100, 2300))
art("SM_Water_A", -700, 2200, -25, yaw=0, scale=(0.22, 0.19, 1.0), folder=f"Art/{S1}/Channel")
# 가로등(따뜻한 조명) — 시작점과 데크를 비춤
for i, yy in enumerate([0, 1300, 2600]):
    art("SM_Slums_Light_01a_OP", 150, yy, 420, yaw=0, folder=f"Art/{S1}/Lamps")
    point(f"S1_Street_{i}", 150, yy, 400, WARM, cd=60.0, radius=1100.0, shadows=True)
marker("Checkpoint_01_Start", 0, 100, 0, "Gameplay/Checkpoints")
marker("Enemy_SpiderBot_S1_01", 0, 2700, 0, "Gameplay/Enemies")

# ---------------- S2 드론 감시 구역 (Y 3000 → 6200) : 2층 구조 + 감시 조명
S2 = "S2_DroneWatch"
col("S2_Ground", LANE_X0, LANE_X1, 3000, 5900, -60, 0, f"Collision/{S2}")
ramp("S2_Stair_Up", 0, 250, 3100, 0, 3412, 182, folder=f"Collision/{S2}")           # 30°
col("S2_Terrace_A", 0, 250, 3412, 4400, 0, 182, f"Collision/{S2}")
col("S2_Terrace_B", 0, 250, 4550, 5900, 0, 182, f"Collision/{S2}")                   # 간격 150, 아래는 앞길과 연결
col("S2_Cover_1", -250, -150, 3900, 4050, 0, 110, f"Collision/{S2}")
col("S2_Cover_2", -250, -150, 5000, 5150, 0, 110, f"Collision/{S2}")
ramp("S2_Stair_ToTower", -250, 0, 5900, 0, 6212, 182, folder=f"Collision/{S2}")
col("S2_Terrace_C", 0, 250, 5900, 6212, 0, 182, f"Collision/{S2}")

ground_tiles(S2, 3000, 5900, 0)
foreground(S2, 3000, 6212)
for xx in (65, 185):
    art("SM_stairs_W1_H3_00", xx, 3256, 0, yaw=0, folder=f"Art/{S2}/Stairs")
for xx in (-160, -30):
    art("SM_stairs_W1_H3_00", xx, 6056, 0, yaw=0, folder=f"Art/{S2}/Stairs")
# 테라스(위층 통로) 윗면 + 정면
for (ya, yb) in [(3412, 4400), (4550, 5900), (5900, 6212)]:
    yy = ya
    while yy < yb - 1:
        for xx in (60, 190):
            art("SM_Slums_Floor_01a", xx, yy + 100, 182 - 64, yaw=0, folder=f"Art/{S2}/Terrace")
        yy += 200
    yy = ya
    while yy < yb - 50:
        n = rng.choice(["SM_Slums_Wall_01a", "SM_Slums_Wall_01d", "SM_Slums_Wall_02a", "SM_Slums_Wall_02c", "SM_Slums_Wall_01f"])
        art(n, 10, yy + span_y(n) / 2, 182 - INV[n]["size"][2], folder=f"Art/{S2}/TerraceFront")
        yy += span_y(n)
# 엄폐물(낮은 나무 벽)
for yy in (3975, 5075):
    art("SM_Slums_WoodWall_02c", -200, yy, 0, yaw=90, folder=f"Art/{S2}/Cover")
    art("SM_Slums_Trashcan_Closed02", -165, yy + 90, 0, yaw=30, folder=f"Art/{S2}/Cover")
# 테라스 난간(시각)
yy = 3450
while yy < 5850:
    if not (4380 < yy < 4570):
        art("SM_Slums_Scaffolding_02a", 20, yy + 253, 182 + 60, yaw=90, folder=f"Art/{S2}/Rail")
    yy += 506
facade_row(S2, 3000, 6200, 300, 182, tiers=2, neon_chance=0.35)
wires(S2, 3000, 6200, 150, 760)
# 서치라이트(감시 구역 시각화 — 차가운 흰빛/붉은 경고)
for i, (yy, rgb) in enumerate([(3800, (0.8, 0.9, 1.0)), (4700, (1.0, 0.15, 0.1)), (5600, (0.8, 0.9, 1.0))]):
    art("SM_S_Searchlight_01", 520, yy, 182, yaw=180, folder=f"Art/{S2}/Searchlights")
    spot(f"S2_Search_{i}", 450, yy, 900, -62, -140 if i % 2 else -165, rgb, cd=900.0, radius=2600.0, outer=16.0, inner=6.0)
    marker(f"DronePatrol_S2_0{i + 1}", 0, yy, 520, "Gameplay/Enemies")
point("S2_Lamp_Low_1", -100, 3500, 260, WARM, cd=35.0, radius=800.0, shadows=True)
point("S2_Lamp_Low_2", -100, 4800, 260, WARM, cd=35.0, radius=800.0, shadows=True)
point("S2_Lamp_Terrace", 150, 5300, 420, COLD, cd=30.0, radius=800.0)
marker("Checkpoint_02_DroneWatch", -100, 3100, 0, "Gameplay/Checkpoints")

# ---------------- S3 전송 타워 외부 (Y 6200 → 8212) : 계단 + 단차 점프로 상승
S3 = "S3_TowerExterior"
col("S3_L1", LANE_X0, LANE_X1, 6212, 6700, 0, 182, f"Collision/{S3}")
ramp("S3_StairB", LANE_X0, LANE_X1, 6700, 182, 7012, 364, folder=f"Collision/{S3}")
col("S3_StairB_Under", LANE_X0, LANE_X1, 6700, 7012, 0, 182, f"Collision/{S3}")
col("S3_L2", LANE_X0, LANE_X1, 7012, 7300, 0, 364, f"Collision/{S3}")
col("S3_Step1", LANE_X0, LANE_X1, 7300, 7500, 0, 419, f"Collision/{S3}")    # +55
col("S3_Step2", LANE_X0, LANE_X1, 7500, 7700, 0, 474, f"Collision/{S3}")    # +55
col("S3_Step3", LANE_X0, LANE_X1, 7700, 7900, 0, 529, f"Collision/{S3}")    # +55
ramp("S3_StairC", LANE_X0, LANE_X1, 7900, 529, 8212, 711, folder=f"Collision/{S3}")
col("S3_StairC_Under", LANE_X0, LANE_X1, 7900, 8212, 0, 529, f"Collision/{S3}")

for xx in (-183, 0, 183):
    art("SM_stairs_W1_H3_00", xx, 6856, 182, yaw=0, folder=f"Art/{S3}/Stairs")
    art("SM_stairs_W1_H3_00", xx, 8056, 529, yaw=0, folder=f"Art/{S3}/Stairs")
# 계단참/발판 윗면(비계 느낌의 철판)
for (ya, yb, top) in [(6212, 6700, 182), (7012, 7300, 364), (7300, 7500, 419), (7500, 7700, 474), (7700, 7900, 529)]:
    yy = ya
    while yy < yb - 1:
        for xx in (-130, 130):
            art("SM_Slums_Floor_01a", xx, yy + 100, top - 64, yaw=0, folder=f"Art/{S3}/Platforms")
        yy += 200
# 단차 발판 정면 네온(점프 목표를 시각적으로 강조)
for k, (yy, top) in enumerate([(7400, 419), (7600, 474), (7800, 529)]):
    neon(f"S3_Step_{k}", -262, yy, top - 8, 190, "Cyan", "Y", thick=5)
# 타워 구조 정면(녹슨 산업 패널) — 각 발판 윗면까지만 채움(플레이어를 가리지 않게)
for (ya, yb, top) in [(6212, 6700, 182), (6700, 7012, 182), (7012, 7300, 364), (7300, 7500, 419),
                      (7500, 7700, 474), (7700, 7900, 529), (7900, 8212, 529)]:
    industrial_wall(S3, ya, yb, -262, -60, top - 10)
# 전송 타워 본체(배경 랜드마크): 거대 환풍 탱크 + 기어 + 비계 + 파란 네온
art("SM_Slums_Vent_01b", 1100, 7300, 0, yaw=90, folder=f"Art/{S3}/Tower")
art("SM_Slums_Vent_01b", 1100, 7300, 1450, yaw=90, scale=0.8, folder=f"Art/{S3}/Tower")
art("SM_LV_Soul_CenterBD_Gear01", 700, 7300, 900, yaw=0, folder=f"Art/{S3}/Tower")
for k, yy in enumerate([6500, 8100]):
    for zz in (0, 576, 1152):
        art("SM_Slums_Scaffolding_03a", 600, yy, zz, yaw=90, folder=f"Art/{S3}/Tower")
for k, zz in enumerate([600, 1200, 1800, 2400]):
    neon(f"S3_TowerRing_{k}", 560, 7300, zz, 1400, "Blue", "Y", thick=10, intensity=40.0)
spot("S3_TowerBeam", 1100, 7300, 3600, -90, 0, (0.2, 0.5, 1.0), cd=1500.0, radius=4200.0, outer=12.0, inner=4.0)
point("S3_TowerGlow", 500, 7300, 1300, (0.15, 0.4, 1.0), cd=120.0, radius=2200.0)
point("S3_Stair_Lamp", -150, 6900, 520, WARM, cd=40.0, radius=900.0, shadows=True)
point("S3_Step_Lamp", -150, 7600, 780, COLD, cd=40.0, radius=900.0, shadows=True)
facade_row(S3, 8212 - 1800, 8212, 1500, -200, tiers=3, neon_chance=0.3)
foreground(S3, 6212, 8212)
marker("ScrapLift_Optional_S3", 150, 6500, 182, "Gameplay/Unimplemented")
marker("Checkpoint_03_Tower", 0, 6400, 182, "Gameplay/Checkpoints")

# ---------------- S4 코어 해킹 구역 (Y 8212 → 10000, z=711)
S4 = "S4_CoreHack"
CORE_Z = 711
col("S4_CorePlatform", LANE_X0, LANE_X1, 8212, 10000, 0, CORE_Z, f"Collision/{S4}")
col("S4_Cover_1", -120, 0, 8700, 8850, CORE_Z, CORE_Z + 110, f"Collision/{S4}")
col("S4_Cover_2", -120, 0, 9450, 9600, CORE_Z, CORE_Z + 110, f"Collision/{S4}")
yy = 8212
while yy < 10000 - 1:
    for xx in (-200, 0, 200):
        art("SM_Slums_Floor_02a", xx, yy + 100, CORE_Z - 18, yaw=0, folder=f"Art/{S4}/Floor")
    yy += 200
industrial_wall(S4, 8212, 10000, -262, -60, CORE_Z - 10)
# 해킹 장치(전송 코어): 캡슐 탱크 + 배관 + 초록 조명
art("SM_Slums_Vent_01b_2", 150, 9150, CORE_Z, yaw=90, folder=f"Art/{S4}/HackDevice")
art("SM_Slums_Pipe_04a", 120, 8950, CORE_Z, yaw=90, folder=f"Art/{S4}/HackDevice")
art("SM_Slums_Pipe_04a", 120, 9350, CORE_Z, yaw=90, folder=f"Art/{S4}/HackDevice")
art("SM_Slums_Pipes_04c", 200, 9000, CORE_Z, yaw=0, folder=f"Art/{S4}/HackDevice")
art("SM_Slums_Sign02d", -40, 9150, CORE_Z + 130, yaw=90, folder=f"Art/{S4}/HackDevice")
neon("S4_Hack_Ring_1", -30, 9150, CORE_Z + 40, 380, "Green", "Y", thick=8, intensity=35.0)
neon("S4_Hack_Ring_2", -30, 9150, CORE_Z + 470, 380, "Green", "Y", thick=8, intensity=35.0)
point("S4_HackGlow", -60, 9150, CORE_Z + 250, (0.2, 1.0, 0.35), cd=80.0, radius=1200.0, shadows=True)
marker("HackTerminal_01", 0, 9150, CORE_Z, "Gameplay/Interactables")
# 엄폐물
for yy in (8775, 9525):
    art("SM_Slums_WoodWall_02a", -60, yy, CORE_Z, yaw=90, folder=f"Art/{S4}/Cover")
# 방어 드론 위치 + 붉은 경고등
for k, yy in enumerate([8700, 9600]):
    marker(f"DefenseDrone_S4_0{k + 1}", 0, yy, CORE_Z + 450, "Gameplay/Enemies")
    point(f"S4_Warn_{k}", 200, yy, CORE_Z + 520, (1.0, 0.08, 0.05), cd=45.0, radius=900.0)
    art("SM_Slums_Light_01a_OP", 200, yy, CORE_Z + 540, yaw=0, folder=f"Art/{S4}/Lights")
# 배경: 거대 기어 + 산업 벽 + 파란 네온
art("SM_LV_Soul_CenterBD_Gear01", 900, 9150, CORE_Z + 150, yaw=0, folder=f"Art/{S4}/Backdrop")
industrial_wall(S4, 8212, 10000, 420, CORE_Z, CORE_Z + 1250, name="SM_Slums_Wall_03a")
for k, zz in enumerate([CORE_Z + 700, CORE_Z + 1100]):
    neon(f"S4_Back_{k}", 400, 9100, zz, 1700, "Cyan", "Y", thick=8, intensity=30.0)
point("S4_Fill", 0, 8500, CORE_Z + 400, COLD, cd=35.0, radius=1300.0)
point("S4_Fill_2", 0, 9800, CORE_Z + 400, COLD, cd=35.0, radius=1300.0)
foreground(S4, 8212, 10000)
marker("Checkpoint_04_Core", 0, 8350, CORE_Z, "Gameplay/Checkpoints")

# ---------------- S5 탈출 경로 (Y 10000 → 12900) : 하강 점프 → 지상 → 최종 게이트
S5 = "S5_Escape"
col("S5_Ground", LANE_X0, LANE_X1, 10000, Y_END, -60, 0, f"Collision/{S5}")          # 떨어지면 출구 쪽 지상으로 이어짐
# 발판 두께 40 → 가장 낮은 E4 아래도 210cm라 떨어진 캐릭터(최대 176cm)가 지상으로 지나갈 수 있다
col("S5_E1", LANE_X0, LANE_X1, 10000, 10400, CORE_Z - 40, CORE_Z, f"Collision/{S5}")
col("S5_E2", LANE_X0, LANE_X1, 10550, 10900, 610, 650, f"Collision/{S5}")           # 간격 150, -61
col("S5_E3", LANE_X0, LANE_X1, 11050, 11350, 440, 480, f"Collision/{S5}")           # 간격 150, -170
col("S5_E4", LANE_X0, LANE_X1, 11500, 11800, 210, 250, f"Collision/{S5}")           # 간격 150, -230
ground_tiles(S5, 10000, Y_END, 0)
foreground(S5, 10000, Y_END)
for (ya, yb, top) in [(10000, 10400, CORE_Z), (10550, 10900, 650), (11050, 11350, 480), (11500, 11800, 250)]:
    yy = ya
    while yy < yb - 1:
        for xx in (-130, 130):
            art("SM_Slums_Floor_01a", xx, yy + 100, top - 64, yaw=0, folder=f"Art/{S5}/Platforms")
        yy += 200
    # 발판을 받치는 비계 기둥
    for yy in (ya + 40, yb - 40):
        for xx in (-200, 200):
            h = top - 64
            art("SM_Slums_Pipe_02a", xx, yy, 0, yaw=0, scale=(1, 1, max(0.2, h / 102.0)), folder=f"Art/{S5}/Supports")
    neon(f"S5_Edge_{ya}", -262, (ya + yb) / 2, top - 8, (yb - ya) - 20, "Orange", "Y", thick=5)
# 추격 연출용 붉은 조명 + 마커
for k, yy in enumerate([10300, 11200]):
    point(f"S5_Chase_{k}", 0, yy, 1000, (1.0, 0.1, 0.05), cd=40.0, radius=1200.0)
    marker(f"ChaseDrone_S5_0{k + 1}", 0, yy, 1000, "Gameplay/Enemies")
facade_row(S5, 10000, Y_END, 300, 0, tiers=3, neon_chance=0.6)
wires(S5, 10000, Y_END, 150, 650)
# 최종 게이트(EXIT): 사이드뷰에서 읽히도록 카메라를 향한 뒷벽의 빛나는 출입구로 만든다
GATE_Y = 12450
GATE_W, GATE_H = 360, 420
for yy in (GATE_Y - GATE_W / 2, GATE_Y + GATE_W / 2):
    art("SM_Slums_Wall_02n", 300, yy, 0, yaw=0, folder=f"Art/{S5}/ExitGate")
art("SM_Slums_Wall_03b", 300, GATE_Y, GATE_H, yaw=90, scale=((GATE_W + 70) / 794, 1.0, 0.3), folder=f"Art/{S5}/ExitGate")
portal = neon("S5_Gate_Portal", 318, GATE_Y, GATE_H / 2, GATE_W - 60, "Cyan", "Y", thick=4, intensity=12.0)
portal.set_actor_scale3d(unreal.Vector(0.04, (GATE_W - 60) / 100, (GATE_H - 20) / 100))
neon("S5_Gate_Top", 262, GATE_Y, GATE_H - 10, GATE_W + 40, "Cyan", "Y", thick=12, intensity=60.0)
for yy in (GATE_Y - GATE_W / 2 - 40, GATE_Y + GATE_W / 2 + 40):
    neon(f"S5_Gate_Side_{int(yy)}", 262, yy, GATE_H / 2, GATE_H, "Cyan", "Z", thick=12, intensity=60.0)
art("SM_Slums_Sign02a", 255, GATE_Y, GATE_H + 60, yaw=90, folder=f"Art/{S5}/ExitGate")
point("S5_GateGlow", 120, GATE_Y, 250, (0.1, 0.9, 1.0), cd=150.0, radius=1600.0, shadows=True)
for k, yy in enumerate([GATE_Y - 900, GATE_Y - 600, GATE_Y - 300]):     # 바닥 유도등
    neon(f"S5_Guide_{k}", -60, yy, 3, 120, "Cyan", "Y", thick=4, intensity=30.0)
art("SM_Slums_Wall_03a", 0, Y_END - 20, 0, yaw=0, folder=f"Art/{S5}/Structure")
marker("ExitGate_ToDistrict4", 0, GATE_Y + 150, 0, "Gameplay/Interactables")
marker("Checkpoint_05_Escape", 0, 10100, CORE_Z, "Gameplay/Checkpoints")

# ================================================================ BACKGROUND (중경/원경)
BG = "Background"
y = Y_START - 2000
while y < Y_END + 2000:     # 중경: 원경 판자촌 파사드(창문 불빛이 있는 LOD 메시)
    n = rng.choice(["SM_ShantyLODFar1", "SM_ShantyLODFar2", "SM_ShantyLODFar3", "SM_ShantyLODFar4", "SM_ShantyLODFar5"])
    art(n, 2600 + rng.uniform(-300, 300), y + span_y(n) / 2, rng.uniform(-500, 300), folder=f"{BG}/Mid")
    y += span_y(n) - 200
y = Y_START - 6000
while y < Y_END + 6000:     # 원경: 판자촌 군집
    art("SM_ShantyLODFarGroup", 7000 + rng.uniform(-800, 800), y + 5234, rng.uniform(-2500, -800), scale=1.2, folder=f"{BG}/Far")
    y += 9000
for k, yy in enumerate(range(-4000, 17000, 7000)):   # 동굴 벽(지하 도시 느낌)
    art("SM_SlumMountain_01", 14000, yy, -2000, yaw=rng.uniform(0, 360), scale=2.5, folder=f"{BG}/Cave")
for k, yy in enumerate(range(-2000, 15000, 3500)):   # 동굴 천장 바위
    art("SM_Cave_Rock_Large02", rng.uniform(3000, 9000), yy, 5200 + rng.uniform(0, 1500), yaw=rng.uniform(0, 360), scale=4.0,
        pitch=180, folder=f"{BG}/Cave")

# ================================================================ ATMOSPHERE
ATM = "Lighting/Atmosphere"
sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 6000, 3000))
sky.light_component.set_editor_property("intensity", 0.6)
sky.light_component.set_editor_property("light_color", _color((0.35, 0.45, 0.7)))
sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
_finish(sky, ATM, "SkyLight_Cave"); _record(sky, "skylight")

moon = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 6000, 3500), unreal.Rotator(pitch=-55, yaw=35, roll=0))
moon.light_component.set_editor_property("intensity", 0.6)
moon.light_component.set_editor_property("light_color", _color((0.5, 0.6, 1.0)))
moon.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
_finish(moon, ATM, "DirLight_CaveShaft"); _record(moon, "dirlight")

fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 6000, -200))
fc = fog.component
fc.set_editor_property("fog_density", 0.03)
fc.set_editor_property("fog_height_falloff", 0.08)
fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.012, 0.022, 0.04, 1.0))
fc.set_editor_property("enable_volumetric_fog", True)
fc.set_editor_property("volumetric_fog_extinction_scale", 1.5)
fc.set_editor_property("volumetric_fog_scattering_distribution", 0.6)
_finish(fog, ATM, "Fog_Underground"); _record(fog, "fog")

ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 6000, 0))
ppv.set_editor_property("unbound", True)
s = ppv.settings
s.set_editor_property("override_auto_exposure_min_brightness", True); s.set_editor_property("auto_exposure_min_brightness", -1.0)
s.set_editor_property("override_auto_exposure_max_brightness", True); s.set_editor_property("auto_exposure_max_brightness", 1.5)
s.set_editor_property("override_bloom_intensity", True); s.set_editor_property("bloom_intensity", 1.2)
s.set_editor_property("override_vignette_intensity", True); s.set_editor_property("vignette_intensity", 0.5)
s.set_editor_property("override_color_saturation", True); s.set_editor_property("color_saturation", unreal.Vector4(1.0, 1.05, 1.15, 1.0))
ppv.set_editor_property("settings", s)
_finish(ppv, ATM, "PPV_Global"); _record(ppv, "ppv")

# ================================================================ GAMEPLAY
ps1 = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-40, 150, 92))
_finish(ps1, "Gameplay/PlayerStarts", "PlayerStart_Brother"); _record(ps1, "playerstart")
ps2 = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-40, -60, 82))
ps2.set_editor_property("player_start_tag", "Sister")
_finish(ps2, "Gameplay/PlayerStarts", "PlayerStart_Sister"); _record(ps2, "playerstart", {"tag": "Sister"})

ws = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_asset("/Game/Project_CX/Characters/Brother/BP_GameMode").generated_class())
ws.set_editor_property("kill_z", -1500.0)

les.save_current_level()

import os
os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
json.dump({"level": LEVEL, "tag": TAG, "actors": manifest}, open(MANIFEST, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
kinds = {}
for e in manifest:
    kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1
print("built", len(manifest), "actors", kinds)
print("manifest ->", MANIFEST)
