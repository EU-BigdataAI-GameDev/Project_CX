"""
Map2 빌더 — .claude/Plans/맵2_레벨디자인.md 의 "무단 증축 판자촌" 구성을 Soul City 에셋(피벗 중심화 버전)으로 구현한다.

좌표계 : +Y 진행(화면 오른쪽), 카메라는 -X 쪽에서 +X를 바라봄, Z 위
레인   : X ∈ [-120, 150]. X < -120 에는 발판 높이를 넘는 메시를 두지 않는다(카메라 가림 방지).
실측   : 여동생 키 160 / 오빠 176, JumpZ 420 → 최고 점프 90cm, 자동 계단 45cm
설계   : 필수 상승 ≤ 55, 필수 간격 ≤ 180, 계단 30°, 여동생 전용 천장 170
조명   : 넣지 않는다(라이트/스카이라이트/포그/PPV/발광 스트립 없음).

※ 태그 GEN_Map2 가 붙은 액터만 지우고 다시 만든다. 사람이 레벨을 수정한 뒤에는 재실행하지 말 것.
"""
import unreal, json, math, random

LEVEL = "/Game/Project_CX/Maps/Map2"
TAG = "GEN_Map2"
MANIFEST = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map2_manifest.json")
CUBE = "/Engine/BasicShapes/Cube.Cube"

LANE_F, LANE_B = -120.0, 150.0     # 플레이 레인 앞/뒤
SPLIT_X = -20.0                     # S2 갈라짐: 앞쪽=여동생 통로, 뒤쪽=오빠 지붕

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eal = unreal.EditorAssetLibrary

INV = {m["path"].split("/")[-1]: m for m in json.load(open(unreal.Paths.project_saved_dir() + "soul_inventory.json"))}
_mesh_cache = {}
def mesh(name):
    if name not in _mesh_cache:
        _mesh_cache[name] = unreal.load_asset(INV[name]["path"] if name in INV else name)
        if _mesh_cache[name] is None:
            raise RuntimeError("mesh not found: " + name)
    return _mesh_cache[name]

rng = random.Random(22)
manifest = []

# ---------------------------------------------------------------- level
if eal.does_asset_exist(LEVEL):
    if les.get_current_level().get_outermost().get_name() != LEVEL:
        les.load_level(LEVEL)
else:
    les.new_level(LEVEL)
for a in eas.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
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

# ---------------------------------------------------------------- art helpers (피벗 = 바운드 중심)
def _extent(name, yaw, sc):
    """yaw(0/90/180/270) 회전 후 월드 X/Y/Z 크기."""
    s = INV[name]["size"]
    if round(yaw) % 180 == 90:
        return s[1] * sc[1], s[0] * sc[0], s[2] * sc[2]
    return s[0] * sc[0], s[1] * sc[1], s[2] * sc[2]

def art(name, cx, cy, bottom_z, yaw=90.0, scale=1.0, folder="Art", label=None, mat=None):
    """바운드 중심을 (cx, cy)에, 바닥을 bottom_z에 둔다. 충돌 없음."""
    sc = tuple(scale) if isinstance(scale, (tuple, list)) else (scale, scale, scale)
    ex, ey, ez = _extent(name, yaw, sc)
    a = eas.spawn_actor_from_object(mesh(name), unreal.Vector(cx, cy, bottom_z + ez / 2), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    a.set_actor_scale3d(unreal.Vector(*sc))
    comp = a.static_mesh_component
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if mat is not None:
        comp.set_material(0, mat)
    _finish(a, folder, label)
    _record(a, "art", {"mesh": name, "bbox": [cx - ex / 2, cx + ex / 2, cy - ey / 2, cy + ey / 2, bottom_z, bottom_z + ez]})
    return a

def fit(name, x0, x1, y0, y1, z0, z1, yaw=90.0, folder="Art", label=None, mat=None):
    """메시를 비균등 스케일해 박스(x0..x1, y0..y1, z0..z1)를 채운다."""
    s = INV[name]["size"]
    wx, wy, wz = max(x1 - x0, 1), max(y1 - y0, 1), max(z1 - z0, 1)
    if round(yaw) % 180 == 90:
        sc = (wy / max(s[0], 1), wx / max(s[1], 1), wz / max(s[2], 1))
    else:
        sc = (wx / max(s[0], 1), wy / max(s[1], 1), wz / max(s[2], 1))
    return art(name, (x0 + x1) / 2, (y0 + y1) / 2, z0, yaw, sc, folder, label, mat)

def tiles(names, x0, x1, y0, y1, top, thick, tile_y=250.0, tile_x=None, folder="Art", yaw=0.0):
    """윗면이 top인 바닥판을 여러 메시로 타일링(시각용)."""
    tile_x = tile_x or (x1 - x0)
    ny = max(1, int(round((y1 - y0) / tile_y)))
    nx = max(1, int(round((x1 - x0) / tile_x)))
    for iy in range(ny):
        for ix in range(nx):
            fit(rng.choice(names), x0 + (x1 - x0) * ix / nx, x0 + (x1 - x0) * (ix + 1) / nx,
                y0 + (y1 - y0) * iy / ny, y0 + (y1 - y0) * (iy + 1) / ny, top - thick, top, yaw=yaw, folder=folder)

def row(names, x, y0, y1, z, scale=1.0, yaw=90.0, folder="Art", gap=0.0, jitter_z=0.0):
    """y0..y1 구간에 메시를 옆으로 이어 붙인다. 넘치는 마지막 하나는 생략."""
    y = y0
    out = []
    while True:
        n = rng.choice(names)
        _, ey, _ = _extent(n, yaw, (scale, scale, scale))
        if y + ey > y1 + 1:
            break
        out.append(art(n, x, y + ey / 2, z + rng.uniform(-jitter_z, jitter_z), yaw, scale, folder))
        y += ey + gap
    return out

# ---------------------------------------------------------------- collision helpers
def _col_setup(a, label, folder):
    comp = a.static_mesh_component
    comp.set_collision_profile_name("BlockAll")
    # 스프링암 충돌 테스트가 투명벽/지형에 걸려 카메라가 당겨지지 않도록
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_editor_property("visible", False)
    comp.set_editor_property("hidden_in_game", True)
    comp.set_editor_property("cast_shadow", False)
    _finish(a, folder, "COL_" + label)

def col(label, x0, x1, y0, y1, z0, z1, folder="Collision", walk=True):
    a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    a.set_actor_scale3d(unreal.Vector((x1 - x0) / 100, (y1 - y0) / 100, (z1 - z0) / 100))
    _col_setup(a, label, folder)
    _record(a, "collision" if walk else "blocker", {"box": [x0, x1, y0, y1, z0, z1]})
    return a

def ramp(label, x0, x1, y0, z0, y1, z1, thick=20, folder="Collision/Stairs"):
    """윗면이 (y0,z0)→(y1,z1)인 경사 충돌."""
    dy, dz = y1 - y0, z1 - z0
    length = math.hypot(dy, dz)
    ang = math.degrees(math.atan2(dz, dy))
    ny, nz = -dz / length, dy / length
    a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2 - ny * thick / 2, (z0 + z1) / 2 - nz * thick / 2))
    a.set_actor_rotation(unreal.Rotator(pitch=ang, yaw=90, roll=0), False)
    a.set_actor_scale3d(unreal.Vector(length / 100, (x1 - x0) / 100, thick / 100))
    _col_setup(a, label, folder)
    _record(a, "collision_ramp", {"x": [x0, x1], "from": [y0, z0], "to": [y1, z1], "angle": round(abs(ang), 1)})
    return a

def stairs_art(x0, x1, y0, z0, y1, z1, folder):
    """SM_stairs_W1_H3_00(런 312 / 라이즈 182, +Y로 상승)을 스케일해 계단 모양을 만든다. 내려가는 계단은 yaw 180."""
    run, rise = abs(y1 - y0), abs(z1 - z0)
    n = max(1, int(round((x1 - x0) / 183)))
    yaw = 0.0 if (z1 - z0) * (y1 - y0) > 0 else 180.0
    for i in range(n):
        fit("SM_stairs_W1_H3_00", x0 + (x1 - x0) * i / n, x0 + (x1 - x0) * (i + 1) / n, min(y0, y1), max(y0, y1),
            min(z0, z1), min(z0, z1) + rise, yaw=yaw, folder=folder)

def under_stairs(x0, x1, y0, z0, y1, z1, base, folder, seg=100.0):
    """레인 일부만 쓰는 계단 밑의 빈 삼각형을 계단식 벽판으로 막는다(앞에서 보이는 면)."""
    n = max(1, int(abs(y1 - y0) // seg))
    for i in range(n):
        ya, yb = y0 + (y1 - y0) * i / n, y0 + (y1 - y0) * (i + 1) / n
        za, zb = z0 + (z1 - z0) * i / n, z0 + (z1 - z0) * (i + 1) / n
        h = min(za, zb) - base - 4
        if h > 20:
            fit(rng.choice(WALLS_CONC), x0, x1, min(ya, yb), max(ya, yb), base, base + h, yaw=90, folder=folder)

def marker(label, x, y, z, folder):
    a = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, z))
    _finish(a, folder, "MARKER_" + label)
    _record(a, "marker")
    return a

# ================================================================ 메시 팔레트
FACADES = ["SM_Slums_Distance_01a", "SM_Slums_Distance_01b", "SM_Slums_Distance_01c", "SM_Slums_Distance_01d",
           "SM_Slums_Distance_02b", "SM_Slums_Distance_02c", "SM_Slums_Distance_02d", "SM_Slums_Distance_03a_OP"]
TALL_FACADES = ["SM_Slums_Distance_02e", "SM_Slums_Distance_05a_Opt", "SM_Slums_Distance_03a_OP"]
ROOFS = ["SM_Slums_Roof_01a", "SM_Slums_Roof_01b", "SM_Slums_Roof_01c"]
WALLS_TIN = ["SM_Slums_Wall_01a", "SM_Slums_Wall_01b", "SM_Slums_Wall_01c", "SM_Slums_Wall_01d", "SM_Slums_Wall_01f"]
WALLS_CONC = ["SM_Slums_Wall_02a", "SM_Slums_Wall_02b", "SM_Slums_Wall_02e", "SM_Slums_Wall_02k"]
WALLS_WIN = ["SM_Slums_Wall_02c", "SM_Slums_Wall_02d", "SM_Slums_Wall_01b", "SM_Slums_Wall_01c"]
FLOORS = ["SM_Slums_Floor_01a", "SM_Slums_Floor_01b", "SM_Slums_Floor_01c", "SM_Slums_Floor_01e", "SM_Slums_Floor_01g", "SM_Slums_Floor_01h"]
GROUND = ["SM_Slums_Floor_02a"]
SIGNS = ["SM_Slums_Sign01a", "SM_Slums_Sign01b_Bent", "SM_Slums_Sign02a", "SM_Slums_Sign02b", "SM_Slums_Sign03a", "SM_Slums_Sign03b",
         "SM_Slums_Sign04a", "SM_Slums_Sign05a", "SM_Slums_Sign06a", "SM_Slums_Sign02d", "SM_Slums_Sign01d_opt"]
CLOTH = ["SM_Slums_Cloth_01a", "SM_Slums_Cloth_01b", "SM_Slums_Cloth_01c", "SM_Slums_Cloth_01d"]
WIRES = ["SM_Slums_Wires_02a", "SM_Slums_Wires_02c", "SM_Slums_Wires_01a"]
JUNK = ["SM_Slums_Trashcan_Closed02", "SM_Slums_Trashcan_Open01", "SM_Slums_Trashcan_Open02", "SM_Slums_Trashbag",
        "SM_Slums_Trash_02c", "SM_Slums_WoodWall_02b", "SM_Slums_WoodWall_02d", "SM_Slums_Pipe_05c", "SM_Slums_Pipe_01a"]
CRATES = ["SM_Slums_WoodWall_02a", "SM_Slums_WoodWall_02c", "SM_Slums_WoodWall_02d"]
MIDGROUND = ["SM_Unified_06_Opt", "SM_Unified_12_opt", "SM_Unified_20_opt", "SM_Unified_24_opt", "SM_Unified_25_Opt",
             "SM_Unified_28_Opt", "SM_Unified_30_Opt", "SM_Unified_31_Opt", "SM_Unified_32_Opt", "SM_Unified_39_Opt", "SM_Unified_22_opt"]
FAR = ["SM_ShantyLODFar1", "SM_ShantyLODFar2", "SM_ShantyLODFar3", "SM_ShantyLODFar4", "SM_ShantyLODFar5"]

Y_START, Y_END = -800.0, 14200.0

def backdrop_house_row(y0, y1, base_z, x=260.0, floors=2, folder="Art/Backdrop"):
    """레인 뒤 판잣집 파사드를 층층이 쌓고 지붕/간판/빨래를 얹는다."""
    z = base_z
    for f in range(floors):
        houses = row(FACADES if f < floors - 1 else FACADES + TALL_FACADES, x + f * 60, y0, y1, z, folder=folder, gap=rng.uniform(0, 30))
        top = z
        for h in houses:
            bb = [e for e in manifest if e["label"] == h.get_actor_label()][-1]["bbox"]
            top = max(top, bb[5])
            if rng.random() < 0.55:
                art(rng.choice(ROOFS), x + f * 60 - 20, (bb[2] + bb[3]) / 2, bb[5] - 10, folder=folder)
            if rng.random() < 0.35:
                art(rng.choice(SIGNS), x + f * 60 - 90, (bb[2] + bb[3]) / 2 + rng.uniform(-80, 80), bb[4] + rng.uniform(180, 300), folder=folder)
        z = z + 440
    for yy in range(int(y0) + 200, int(y1) - 200, 700):
        art(rng.choice(WIRES), x - 60, yy, base_z + rng.uniform(420, 600), folder=folder)

def midground(y0, y1, base_z, x=1500.0, folder="Art/Midground"):
    y = y0
    while y < y1:
        n = rng.choice(MIDGROUND)
        yaw = rng.choice([0.0, 90.0, 180.0, 270.0])
        ex, ey, ez = _extent(n, yaw, (1, 1, 1))
        art(n, x + rng.uniform(-200, 400) + ex / 2, y + ey / 2, base_z + rng.uniform(-80, 0), yaw=yaw, folder=folder)
        y += ey * 0.85

def far(y0, y1, base_z, folder="Art/Far"):
    y = y0
    while y < y1:
        n = rng.choice(FAR)
        ex, ey, ez = _extent(n, 90.0, (1.4, 1.4, 1.4))
        art(n, 4200 + rng.uniform(-300, 600), y + ey / 2, base_z + rng.uniform(-100, 0), scale=1.4, folder=folder)
        y += ey * 0.9

# ================================================================ 경계 (레인 앞/뒤, 양 끝)
col("Bound_Front", LANE_F - 20, LANE_F, Y_START, Y_END, -400, 2600, folder="Collision/Bounds", walk=False)
col("Bound_Back", LANE_B, LANE_B + 20, Y_START, Y_END, -400, 2600, folder="Collision/Bounds", walk=False)
col("Bound_Start", -200, 250, Y_START - 20, Y_START, -400, 2600, folder="Collision/Bounds", walk=False)
col("Bound_End", -200, 250, Y_END, Y_END + 20, -400, 2600, folder="Collision/Bounds", walk=False)

# ================================================================ S0 진입 + S1 침수 수로 (Y -800 ~ 2400)
S1 = "S1_FloodedCanal"
CANAL_Y0, CANAL_Y1, CANAL_FLOOR, WATER_Z = 600.0, 2400.0, -210.0, -140.0
col("S0_Ground", LANE_F - 20, LANE_B + 20, Y_START, CANAL_Y0, -260, 0, folder=f"Collision/{S1}")
col("S1_CanalFloor", LANE_F - 20, LANE_B + 20, CANAL_Y0, CANAL_Y1, -260, CANAL_FLOOR, folder=f"Collision/{S1}")
# 수로 끝 돌계단(42cm × 5) — 빠져도 앞으로 걸어 나온다
for i, top in enumerate([-168, -126, -84, -42]):
    col(f"S1_ExitStep_{i+1}", LANE_F - 20, LANE_B + 20, 2240 + i * 40, 2280 + i * 40, CANAL_FLOOR, top, folder=f"Collision/{S1}")
    art("SM_Cave_Rock_Flat01", 15, 2260 + i * 40, top - 68, yaw=0, scale=(0.8, 0.13, 1.0), folder=f"Art/{S1}/ExitSteps")
# 체인에 매달린 판자 발판 (두께 20, 아래는 수로 — 오빠도 걸어서 통과 가능한 높이)
PALLETS = [("P1", 780, 1040, 0), ("P2", 1200, 1400, 50), ("P3", 1560, 1820, 0), ("P4", 1980, 2240, 40)]
for name, y0, y1, top in PALLETS:
    col(f"S1_Pallet_{name}", -100, 120, y0, y1, top - 20, top, folder=f"Collision/{S1}")
    fit(rng.choice(FLOORS), -100, 120, y0, y1, top - 22, top, yaw=90, folder=f"Art/{S1}/Pallets")
    for cy in (y0 + 30, y1 - 30):   # 체인: 판자 뒤쪽 모서리에서 위로
        art("SM_Cave_Chain_02", 105, cy, top, yaw=90, scale=(0.12, 0.6, 0.55), folder=f"Art/{S1}/Chains")
# 바닥 타일(앞쪽은 발판 높이 그대로 X -420까지 — 가리지 않는 바닥판)
tiles(GROUND, -420, LANE_B + 30, Y_START, CANAL_Y0, 0, 18, tile_y=200, tile_x=190, folder=f"Art/{S1}/Ground")
tiles(GROUND, -420, LANE_B + 30, CANAL_Y1, 2560, 0, 18, tile_y=160, tile_x=190, folder=f"Art/{S1}/Ground")
# 수로: 수면, 바닥, 양쪽 옹벽(앞쪽은 수면 아래까지만)
WATER_MAT = mesh("SM_Water_A").static_materials[0].material_interface
for yy0 in range(int(CANAL_Y0), int(CANAL_Y1), 600):
    fit("SM_Cave_Floor_Plane_100_10", -700, 1400, yy0, min(yy0 + 600, CANAL_Y1), WATER_Z, WATER_Z + 1, yaw=0, folder=f"Art/{S1}/Water", mat=WATER_MAT)
fit("SM_Slums_Wall_02k", -700, 700, CANAL_Y0 - 22, CANAL_Y0, CANAL_FLOOR, 0, yaw=0, folder=f"Art/{S1}/CanalWalls")
fit("SM_Slums_Wall_02k", -700, 700, CANAL_Y1, CANAL_Y1 + 22, CANAL_FLOOR, 0, yaw=0, folder=f"Art/{S1}/CanalWalls")
tiles(["SM_Cave_Floor_Plane_100_10"], -700, 700, CANAL_Y0, CANAL_Y1, CANAL_FLOOR + 1, 1, tile_y=450, tile_x=700, folder=f"Art/{S1}/CanalBed")
for yy in (900, 1500, 2100):
    art(rng.choice(["SM_Cave_Rock_Small01", "SM_Cave_Rock_Small02"]), 60, yy, CANAL_FLOOR, yaw=rng.choice([0.0, 90.0, 180.0]),
        scale=0.8, folder=f"Art/{S1}/CanalBed")
# 시작 계단참 뒤: 거대한 금속 차단벽 + 입구 표지
fit("SM_Slums_Wall_03a", LANE_F, 700, Y_START - 60, Y_START, 0, 900, yaw=0, folder=f"Art/{S1}/Structure")
art("SM_LV_Soul_Slum_signpost", 200, -300, 0, yaw=90, folder=f"Art/{S1}/Props")
for yy in (-620, -420, 250, 420):
    art(rng.choice(JUNK), rng.uniform(110, 140), yy, 0, yaw=rng.choice([0.0, 90.0, 180.0]), folder=f"Art/{S1}/Props")
# 수상 가옥: 물 위 기둥(파이프) + 판잣집 1~2층
for i, (yy0, yy1) in enumerate([(650, 1450), (1550, 2350)]):
    for yy in range(int(yy0) + 50, int(yy1), 180):
        art("SM_Slums_Pipe_02a", 245, yy, WATER_Z, yaw=0, scale=(1.4, 1.4, 1.9), folder=f"Art/{S1}/StiltHouses")
    fit(rng.choice(FLOORS), 200, 480, yy0, yy1, 30, 60, yaw=90, folder=f"Art/{S1}/StiltHouses")
    backdrop_house_row(yy0, yy1, 60, x=330, floors=2, folder=f"Art/{S1}/StiltHouses")
backdrop_house_row(Y_START + 60, 560, 0, x=280, floors=3, folder=f"Art/{S1}/Backdrop")
for yy in (1000, 1700):
    art(rng.choice(CLOTH), 190, yy, 330, folder=f"Art/{S1}/Laundry")
midground(Y_START, 2600, -60, folder=f"Art/{S1}/Midground")

# ================================================================ S2 갈라짐 (Y 2400 ~ 6000)
S2 = "S2_Split"
R = {"R1": (3000, 3700, 260), "PitA": (3700, 3870, 215), "R2": (3870, 4400, 310), "R3": (4400, 4600, 365),
     "PitB": (4600, 4780, 255), "R4": (4780, 5300, 300)}
DUCT_Y0, DUCT_Y1, DUCT_H = 3000.0, 5600.0, 170.0
col("S2_Ground", LANE_F - 20, LANE_B + 20, 2400, 9400, -60, 0, folder=f"Collision/{S2}")
# 오빠 지붕 루트(뒤쪽 X -20~150): 판잣집 덩어리 윗면
for k, (y0, y1, top) in R.items():
    col(f"S2_Roof_{k}", SPLIT_X, LANE_B + 20, y0, y1, 0, top, folder=f"Collision/{S2}/BrotherRoofs")
ramp("S2_StairUp", SPLIT_X, LANE_B, 2550, 0, 3000, 260)
col("S2_StairUp_Seal", SPLIT_X, SPLIT_X + 10, 2550, 3000, 0, 260, folder=f"Collision/{S2}", walk=False)
ramp("S2_StairDown", SPLIT_X, LANE_B, 5300, 300, 5820, 0)
col("S2_StairDown_Seal", SPLIT_X, SPLIT_X + 10, 5300, 5820, 0, 300, folder=f"Collision/{S2}", walk=False)
# 여동생 통로(앞쪽 X -120~-20): 천장 170 처마
col("S2_DuctCeiling", LANE_F - 20, SPLIT_X, DUCT_Y0, DUCT_Y1, DUCT_H, DUCT_H + 20, folder=f"Collision/{S2}/SisterDuct")
# 오빠가 지붕에서 처마 위로 내려가 우회하지 못하게
col("S2_AwningBlock", SPLIT_X - 20, SPLIT_X, DUCT_Y0, DUCT_Y1, DUCT_H + 20, 2600, folder=f"Collision/{S2}", walk=False)

tiles(GROUND, -420, LANE_B + 30, 2560, 6000, 0, 18, tile_y=200, tile_x=190, folder=f"Art/{S2}/Ground")
stairs_art(SPLIT_X, LANE_B, 2550, 0, 3000, 260, folder=f"Art/{S2}/Stairs")
stairs_art(SPLIT_X, LANE_B, 5300, 300, 5820, 0, folder=f"Art/{S2}/Stairs")
under_stairs(SPLIT_X, SPLIT_X + 12, 2550, 0, 3000, 260, 0, folder=f"Art/{S2}/Stairs")
under_stairs(SPLIT_X, SPLIT_X + 12, 5300, 300, 5820, 0, 0, folder=f"Art/{S2}/Stairs")
# 지붕 윗면 + 통로 쪽을 향한 판잣집 정면(= 통로 뒷벽)
for k, (y0, y1, top) in R.items():
    tiles(FLOORS, SPLIT_X, LANE_B, y0, y1, top, 30, tile_y=260, folder=f"Art/{S2}/Roofs")
    if k.startswith("Pit"):
        fit(rng.choice(CRATES), SPLIT_X + 20, LANE_B, y0 + 10, y1 - 10, top - 40, top, yaw=90, folder=f"Art/{S2}/Alleys")
        fit(rng.choice(["SM_Slums_WoodWall_01a", "SM_Slums_WoodWall_01f"]), SPLIT_X, SPLIT_X + 15, y0, y1, 0, top - 40, yaw=90, folder=f"Art/{S2}/Alleys")
    else:
        y = y0
        while y < y1 - 10:
            n = rng.choice(WALLS_TIN + WALLS_WIN)
            w = min(300.0, y1 - y)
            fit(n, SPLIT_X, SPLIT_X + 30, y, y + w, 0, top - 30, yaw=90, folder=f"Art/{S2}/HouseFronts")
            y += w
        # 지붕 위 잡동사니(뒤쪽 절반) — 환기구/안테나
        art(rng.choice(["SM_Slums_Vent_02b", "SM_Slums_Vent_02c", "SM_Slums_Vent_02d"]), 135, (y0 + y1) / 2 + rng.uniform(-80, 80), top, folder=f"Art/{S2}/RoofClutter")
        art("SM_Slums_Pipe_04a", 135, y0 + 60, top, yaw=0, scale=(1, 1, 0.6), folder=f"Art/{S2}/RoofClutter")
# 처마(여동생 통로 천장): 낮은 양철 판 — 오빠 발(260) 아래로 유지
y = DUCT_Y0
while y < DUCT_Y1 - 10:
    w = min(377.0, DUCT_Y1 - y)
    fit(rng.choice(ROOFS), LANE_F, SPLIT_X, y, y + w, DUCT_H, DUCT_H + 70, yaw=90, folder=f"Art/{S2}/SisterDuct")
    y += w
# 통로 안: 처마 밑 배관, 쓰레기통(뒤쪽에 붙여 동선 방해 없음)
for yy in range(int(DUCT_Y0) + 150, int(DUCT_Y1) - 100, 420):
    art(rng.choice(["SM_Slums_Pipe_05g", "SM_Slums_Pipe_05h", "SM_Slums_Pipe_05e"]), SPLIT_X + 5, yy, DUCT_H - 50, yaw=0, folder=f"Art/{S2}/SisterDuct")
# 갈림길 표지: 통로 입구 위 간판 / 계단 옆 빨래
art("SM_Slums_Sign04a", SPLIT_X + 15, DUCT_Y0 + 150, 90, yaw=90, folder=f"Art/{S2}/Fork")
art(rng.choice(CLOTH), 90, 2700, 420, folder=f"Art/{S2}/Fork")
# 지붕 위 빨래줄(머리 위 높이)
for yy, top in ((3350, 260), (4130, 310), (5040, 300)):
    art(rng.choice(CLOTH), 60, yy, top + 290, folder=f"Art/{S2}/Laundry")
    art(rng.choice(WIRES), 60, yy + 150, top + 330, folder=f"Art/{S2}/Laundry")
# 뒤쪽 판잣집(지붕 루트보다 높게)
backdrop_house_row(2450, 5950, 0, x=300, floors=3, folder=f"Art/{S2}/Backdrop")
midground(2600, 6200, -60, folder=f"Art/{S2}/Midground")

# ================================================================ S3 단면 공동주택 (Y 6000 ~ 9400)
S3 = "S3_Tenement"
F2, F3, ROOF = 364.0, 728.0, 1092.0
SLAB = 30.0
B_Y0, B_Y1 = 6000.0, 9400.0
STAIR_X0 = 20.0
# 좌측 외벽(문 위만 막힘, 문 높이 250) / 우측 외벽(3층 문만 열림)
col("S3_WallLeft_AboveDoor", LANE_F - 20, LANE_B + 20, B_Y0 - 20, B_Y0 + 10, 250, ROOF + SLAB, folder=f"Collision/{S3}", walk=False)
col("S3_WallRight_Lower", LANE_F - 20, LANE_B + 20, B_Y1 - 10, B_Y1 + 20, 0, F3, folder=f"Collision/{S3}", walk=False)
col("S3_WallRight_AboveDoor", LANE_F - 20, LANE_B + 20, B_Y1 - 10, B_Y1 + 20, F3 + 250, ROOF + SLAB, folder=f"Collision/{S3}", walk=False)
# 1층 잡동사니 더미(+50)
col("S3_F1_Junk", LANE_F, LANE_B, 6500, 6700, 0, 50, folder=f"Collision/{S3}")
# 2층 바닥: 입구 위 천장 / 계단 A 이후(구멍 7900~8050)
col("S3_F2_Ceiling", LANE_F, LANE_B, B_Y0 + 10, 7000, F2 - SLAB, F2, folder=f"Collision/{S3}")
col("S3_F2_A", LANE_F, LANE_B, 7630, 7900, F2 - SLAB, F2, folder=f"Collision/{S3}")
col("S3_F2_B", LANE_F, LANE_B, 8050, B_Y1 - 10, F2 - SLAB, F2, folder=f"Collision/{S3}")
ramp("S3_StairA", STAIR_X0, LANE_B, 7000, 0, 7630, F2)
col("S3_StairA_Seal", STAIR_X0 - 10, STAIR_X0, 7000, 7630, 0, F2 - SLAB, folder=f"Collision/{S3}", walk=False)
# 3층 바닥: 2층 위 천장(7630~8400) / 계단 B 이후
col("S3_F3_Ceiling", LANE_F, LANE_B, 7630, 8400, F3 - SLAB, F3, folder=f"Collision/{S3}")
col("S3_F3", LANE_F, LANE_B, 9030, B_Y1 - 10, F3 - SLAB, F3, folder=f"Collision/{S3}")
ramp("S3_StairB", STAIR_X0, LANE_B, 8400, F2, 9030, F3)
col("S3_StairB_Seal", STAIR_X0 - 10, STAIR_X0, 8400, 9030, F2, F3 - SLAB, folder=f"Collision/{S3}", walk=False)
col("S3_Roof", LANE_F, LANE_B, B_Y0 - 20, B_Y1 + 20, ROOF, ROOF + SLAB, folder=f"Collision/{S3}", walk=False)

tiles(GROUND, -420, LANE_B + 30, 6000, B_Y1, 0, 18, tile_y=200, tile_x=190, folder=f"Art/{S3}/Ground")
for (y0, y1, top) in [(B_Y0 + 10, 7000, F2), (7630, 7900, F2), (8050, B_Y1 - 10, F2), (7630, 8400, F3), (9030, B_Y1 - 10, F3)]:
    tiles(FLOORS, LANE_F, LANE_B, y0, y1, top, SLAB + 4, tile_y=260, folder=f"Art/{S3}/Slabs")
tiles(["SM_Slums_Floor_02a"], LANE_F, LANE_B + 40, B_Y0 - 20, B_Y1 + 20, ROOF + SLAB, SLAB, tile_y=400, folder=f"Art/{S3}/Roof")
stairs_art(STAIR_X0, LANE_B, 7000, 0, 7630, F2, folder=f"Art/{S3}/Stairs")
stairs_art(STAIR_X0, LANE_B, 8400, F2, 9030, F3, folder=f"Art/{S3}/Stairs")
under_stairs(STAIR_X0 - 10, STAIR_X0, 7000, 0, 7630, F2, 0, folder=f"Art/{S3}/Stairs")
under_stairs(STAIR_X0 - 10, STAIR_X0, 8400, F2, 9030, F3, F2, folder=f"Art/{S3}/Stairs")
# 뒷벽(창문 벽)을 층마다
for fz in (0.0, F2, F3):
    y = B_Y0
    while y < B_Y1 - 10:
        n = rng.choice(WALLS_WIN + WALLS_CONC + WALLS_TIN)
        w = min(300.0, B_Y1 - y)
        fit(n, LANE_B, LANE_B + 35, y, y + w, fz, fz + 364, yaw=90, folder=f"Art/{S3}/BackWall")
        y += w
# 좌우 외벽(단면으로 보이는 벽 두께)
fit("SM_Slums_Wall_02m", LANE_F, LANE_B + 35, B_Y0 - 25, B_Y0 + 5, 250, ROOF + SLAB, yaw=0, folder=f"Art/{S3}/SideWalls")
fit("SM_Slums_Wall_02m", LANE_F, LANE_B + 35, B_Y1 - 5, B_Y1 + 25, 0, F3, yaw=0, folder=f"Art/{S3}/SideWalls")
fit("SM_Slums_Wall_02m", LANE_F, LANE_B + 35, B_Y1 - 5, B_Y1 + 25, F3 + 250, ROOF + SLAB, yaw=0, folder=f"Art/{S3}/SideWalls")
# 단면 앞면 표시: 슬래브 앞 모서리 아래 배관(레인 안쪽, 천장 높이)
# 천장 소품은 실제로 슬래브가 있는 구간에만
for fz, spans in ((F2, [(B_Y0 + 10, 7000), (7630, 7900), (8050, B_Y1 - 10)]), (F3, [(7630, 8400), (9030, B_Y1 - 10)])):
    for y0, y1 in spans:
        for yy in range(int(y0) + 120, int(y1) - 60, 450):
            art(rng.choice(["SM_Slums_Cieling_Pipes_01a", "SM_Slums_Cieling_02a", "SM_Slums_Cieling_03c"]), 60, yy, fz - SLAB - 34, yaw=90, folder=f"Art/{S3}/Ceiling")
for yy in range(int(B_Y0) + 300, int(B_Y1) - 200, 700):
    art(rng.choice(["SM_Slums_Cieling_Pipes_01a", "SM_Slums_Cieling_01a"]), 60, yy, ROOF - 34, yaw=90, folder=f"Art/{S3}/Ceiling")
# 1층: 잡동사니 더미(충돌과 일치), 뒤쪽 소품
fit("SM_Slums_WoodWall_02c", LANE_F + 10, 30, 6500, 6700, 0, 50, yaw=90, folder=f"Art/{S3}/F1")
fit("SM_Slums_WoodWall_02a", 30, LANE_B - 5, 6500, 6700, 0, 50, yaw=90, folder=f"Art/{S3}/F1")
for yy in (6250, 6900, 7700, 8300, 8900):
    art(rng.choice(JUNK), 125, yy, 0, yaw=rng.choice([0.0, 90.0, 180.0]), folder=f"Art/{S3}/F1")
# 2층: 빨래 걸린 방, 환풍기, 쓰레기 / 구멍 가장자리 표시
for yy in (7700, 8200, 8800):
    art(rng.choice(CLOTH), 110, yy, F2 + 190, scale=0.7, folder=f"Art/{S3}/F2")
for yy in (7750, 8600):
    art(rng.choice(["SM_Slums_Vent_02e", "SM_Slums_Vent_02b"]), LANE_B - 10, yy, F2 + 140, yaw=90, folder=f"Art/{S3}/F2")
# 3층: 출구 문틀 + 창문
art("SM_Slums_Window_01a", LANE_B - 5, 9250, F3 + 60, yaw=90, folder=f"Art/{S3}/F3")
art("SM_Slums_Sign04a", LANE_B - 10, 9250, F3 + 260, yaw=90, folder=f"Art/{S3}/F3")
# 건물 외관: 지붕 위 물탱크/안테나, 뒤쪽 이웃 건물
art("SM_Slums_Vent_01b_OP", 60, 6600, ROOF + SLAB, yaw=90, scale=0.8, folder=f"Art/{S3}/RoofTop")
art("SM_Slums_Pipe_04a", 100, 8600, ROOF + SLAB, yaw=0, folder=f"Art/{S3}/RoofTop")
backdrop_house_row(6000, 9400, 0, x=420, floors=3, folder=f"Art/{S3}/Backdrop")
midground(6000, 9600, -60, folder=f"Art/{S3}/Midground")

# ================================================================ S4 옥상 빨래 시장 (Y 9400 ~ 11400, z 728)
S4 = "S4_RooftopMarket"
TOP = F3
col("S4_Block_A", LANE_F - 20, LANE_B + 20, B_Y1 + 20, 10500, 0, TOP, folder=f"Collision/{S4}")
col("S4_Yard", LANE_F - 20, LANE_B + 20, 10500, 10900, 0, TOP - 45, folder=f"Collision/{S4}")
col("S4_Block_B", LANE_F - 20, LANE_B + 20, 10900, 11400, 0, TOP, folder=f"Collision/{S4}")
col("S4_Stage", 20, LANE_B, 9900, 10300, TOP, TOP + 50, folder=f"Collision/{S4}")
# 엄폐물: 뒤쪽 절반(X 40~150)만 막음 → 앞쪽으로는 지나갈 수 있다
COVERS = [("C1", 10000, 10080, TOP + 50), ("C2", 10650, 10730, TOP - 45), ("C3", 11050, 11130, TOP)]
for n, y0, y1, z0 in COVERS:
    col(f"S4_Cover_{n}", 40, LANE_B, y0, y1, z0, z0 + 110, folder=f"Collision/{S4}/Cover")
    fit(rng.choice(CRATES), 40, LANE_B, y0, y1, z0, z0 + 110, yaw=90, folder=f"Art/{S4}/Cover")
tiles(["SM_Slums_Floor_02a"], -420, LANE_B + 30, B_Y1 + 20, 10500, TOP, 18, tile_y=220, tile_x=190, folder=f"Art/{S4}/Floor")
tiles(["SM_Slums_Floor_02a"], -420, LANE_B + 30, 10500, 10900, TOP - 45, 18, tile_y=200, tile_x=190, folder=f"Art/{S4}/Floor")
tiles(["SM_Slums_Floor_02a"], -420, LANE_B + 30, 10900, 11400, TOP, 18, tile_y=250, tile_x=190, folder=f"Art/{S4}/Floor")
tiles(FLOORS, 20, LANE_B, 9900, 10300, TOP + 50, 50, tile_y=200, folder=f"Art/{S4}/Stage")
# 건물 앞면(발밑 아래, 레인 앞 경계에 붙은 외벽) — 발판 높이를 넘지 않음
y = B_Y1 + 20
while y < 11400 - 10:
    w = min(300.0, 11400 - y)
    top = TOP - 45 if 10500 <= y < 10900 else TOP
    fit(rng.choice(WALLS_CONC + WALLS_WIN), LANE_F - 20, LANE_F, y, y + w, 0, top - 18, yaw=90, folder=f"Art/{S4}/FrontFacade")
    y += w
# 시장 노점: 뒤쪽 벽을 따라 비계 + 천막 + 간판
for yy in range(9600, 11300, 520):
    art("SM_Slums_Scaffolding_03b", LANE_B + 110, yy, TOP, yaw=90, folder=f"Art/{S4}/Stalls")
    art(rng.choice(CLOTH), LANE_B + 60, yy, TOP + 260, folder=f"Art/{S4}/Stalls")
    art(rng.choice(SIGNS), LANE_B + 40, yy + 200, TOP + rng.uniform(250, 380), folder=f"Art/{S4}/Stalls")
# 빨래줄(머리 위) — 레인 위를 가로지르는 전선 + 천
for yy in (9700, 10250, 10800, 11250):
    art(rng.choice(WIRES), 20, yy, TOP + 330, folder=f"Art/{S4}/Laundry")
    art(rng.choice(CLOTH), 40, yy + 60, TOP + 285, scale=0.8, folder=f"Art/{S4}/Laundry")
# 원거리 로봇 저격 위치: 물탱크(레인 뒤, 높은 곳)
art("SM_Slums_Vent_01b", 520, 11150, TOP, yaw=90, scale=0.55, folder=f"Art/{S4}/WaterTower")
art("SM_Slums_Scaffolding_03a", 520, 11150, TOP, yaw=90, scale=(1.3, 1.3, 1.0), folder=f"Art/{S4}/WaterTower")
backdrop_house_row(9400, 11400, TOP, x=380, floors=2, folder=f"Art/{S4}/Backdrop")
midground(9400, 11600, -60, x=1300, folder=f"Art/{S4}/Midground")

# ================================================================ S5 암반 하강 (Y 11400 ~ 14200)
S5 = "S5_CaveDescent"
col("S5_ShackRoof", LANE_F - 20, LANE_B + 20, 11400, 11900, 0, 540, folder=f"Collision/{S5}")
ramp("S5_Stair", LANE_F - 20, LANE_B + 20, 11900, 540, 12350, 280)
col("S5_StairFill", LANE_F - 20, LANE_B + 20, 11900, 12350, 0, 280, folder=f"Collision/{S5}")
col("S5_RockA", LANE_F - 20, LANE_B + 20, 12350, 12750, 0, 200, folder=f"Collision/{S5}")
col("S5_RockPit", LANE_F - 20, LANE_B + 20, 12750, 12900, 0, 65, folder=f"Collision/{S5}")
col("S5_RockB", LANE_F - 20, LANE_B + 20, 12900, 13300, 0, 110, folder=f"Collision/{S5}")
col("S5_Ground", LANE_F - 20, LANE_B + 20, 13300, Y_END, -60, 0, folder=f"Collision/{S5}")
# 판잣집 지붕(낙하 착지점)
tiles(FLOORS, LANE_F, LANE_B + 30, 11400, 11900, 540, 40, tile_y=250, folder=f"Art/{S5}/ShackRoof")
y = 11400
while y < 11890:
    fit(rng.choice(WALLS_TIN), LANE_F - 20, LANE_F, y, y + 250, 0, 500, yaw=90, folder=f"Art/{S5}/ShackFront")
    y += 250
stairs_art(LANE_F, LANE_B, 11900, 540, 12350, 280, folder=f"Art/{S5}/Stair")
fit("SM_Slums_Wall_02k", LANE_F - 20, LANE_B + 20, 11900, 12350, 0, 280, yaw=90, folder=f"Art/{S5}/StairBase")
# 바위 턱: 윗면을 충돌과 맞춘 바위 덩어리
for y0, y1, top, n in [(12350, 12750, 200, "SM_Cave_Rock_Medium02"), (12750, 12900, 65, "SM_Cave_Rock_Small01"), (12900, 13300, 110, "SM_Cave_Rock_Large01_REDO")]:
    fit("SM_Slums_Wall_02o", LANE_F, LANE_B + 60, y0, y1, 0, top - 8, yaw=0, folder=f"Art/{S5}/Ledges")
    art(n, LANE_B + 150, (y0 + y1) / 2, top - 30, yaw=0, scale=0.7, folder=f"Art/{S5}/Rocks")
    art("SM_Cave_Rock_Flat01", 15, (y0 + y1) / 2, top - 55, yaw=0, scale=(0.75, min(1.0, (y1 - y0) / 345), 0.9), folder=f"Art/{S5}/Rocks")
tiles(["SM_Cave_Floor_Plane_100_10"], -420, LANE_B + 30, 13300, Y_END, 0, 1, tile_y=450, tile_x=570, folder=f"Art/{S5}/Ground")
for yy in (13450, 13700):
    art(rng.choice(["SM_S_Soul_Flatrock", "SM_Cave_Rock_Small01", "SM_Cave_StoneCluster02"]), 110, yy, 0, yaw=0, folder=f"Art/{S5}/Props")
# 동굴 벽/바위(뒤쪽)
for yy, n, sc in [(12000, "SM_Cave_Rock_Large02", 0.9), (12700, "SM_Cave_Rock_CurvedWall", 1.0), (13400, "SM_Cave_Rock_Large01_REDO", 1.1),
                  (13900, "SM_EntranceRockCliff5", 0.6), (12300, "SM_Cave_Rock_Medium02", 1.0)]:
    art(n, 250 + INV[n]["size"][0] * sc / 2, yy, -50, yaw=0, scale=sc, folder=f"Art/{S5}/CaveWalls")
art("SM_EntranceRockCliff3", 3000, 13000, -400, yaw=90, folder=f"Art/{S5}/CaveWalls")
# 출구 게이트(Map3 산업 구역): 금속 벽 + 문틀 기둥
fit("SM_Slums_Wall_03a", LANE_F, 700, Y_END, Y_END + 64, 0, 700, yaw=0, folder=f"Art/{S5}/ExitGate")
fit("SM_Slums_Wall_03b", LANE_F, LANE_B, Y_END - 40, Y_END, 260, 700, yaw=0, folder=f"Art/{S5}/ExitGate")
for xx in (LANE_F + 45, LANE_B - 30):
    art("SM_Slums_Wall_02n", xx, Y_END - 40, 0, yaw=0, scale=(0.8, 0.8, 0.6), folder=f"Art/{S5}/ExitGate")
art("SM_Slums_Sign02d", 100, Y_END - 60, 300, yaw=90, scale=0.6, folder=f"Art/{S5}/ExitGate")
midground(11400, 13000, -60, x=1600, folder=f"Art/{S5}/Midground")

# ================================================================ 뒤쪽 바닥
GROUND_MAT = None
for y0, y1, z, x0 in [(Y_START - 400, CANAL_Y0, 0, LANE_B + 30), (CANAL_Y0, CANAL_Y1, 0, 1400), (CANAL_Y1, 9400, 0, LANE_B + 30),
                      (11400, Y_END + 400, 0, LANE_B + 30)]:
    tiles(["SM_Cave_Floor_Plane_100_10"], x0, 1400, y0, y1, z, 1, tile_y=600, tile_x=600, folder="Art/BackGround")
# S4 옥상 뒤: 같은 높이의 옥상 데크(가장자리 너머로 아래 도시가 보임)
tiles(["SM_Slums_Floor_02a"], LANE_B + 30, 1000, 9400, 11400, TOP, 18, tile_y=400, tile_x=430, folder="Art/BackGround")
fit("SM_Cave_Floor_Plane_100_10", 1400, 9500, Y_START - 3000, Y_END + 3000, 0, 1, yaw=0, folder="Art/BackGround")

# ================================================================ 원경(맵 전체)
far(Y_START - 1500, Y_END + 1500, -150, folder="Art/Far")
for yy in range(int(Y_START), int(Y_END) + 3000, 3200):
    art(rng.choice(["SM_Cave_Rock_CurvedWall", "SM_Cave_Rock_Large02"]), 7500, yy, -300, yaw=rng.choice([0.0, 90.0, 180.0]), scale=3.0, folder="Art/Far/CaveWall")

# ================================================================ 마커(미구현 기능)
marker("Checkpoint_Map2_01", 0, -400, 20, "Checkpoints")
marker("Checkpoint_Map2_02", 0, 2500, 20, "Checkpoints")
marker("Checkpoint_Map2_03", 0, 5900, 20, "Checkpoints")
marker("Checkpoint_Map2_04", 0, 9200, F3 + 20, "Checkpoints")
marker("Checkpoint_Map2_05", 0, 11600, 560, "Checkpoints")
marker("Enemy_SpiderBot_S2_Roof", 60, 4100, 330, "Enemies")
marker("Enemy_SpiderBot_S3_F1", 0, 6900, 20, "Enemies")
marker("Enemy_RangedBot_S4_01", 520, 11150, TOP + 560, "Enemies")
marker("HackTerminal_01", -70, 5500, 20, "Puzzles")
marker("HackDoor_01", 60, 5320, 320, "Puzzles")
marker("RespawnVolume_S1_Canal", 0, 1500, -150, "Interactables")
marker("ExitGate_ToMap3", 0, Y_END - 100, 20, "Interactables")

# ================================================================ 시작 위치 / 월드 설정
ps1 = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -350, 92))
_finish(ps1, "PlayerStarts", "PlayerStart_Brother"); _record(ps1, "playerstart")
ps2 = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -550, 82))
ps2.set_editor_property("player_start_tag", "Sister")
_finish(ps2, "PlayerStarts", "PlayerStart_Sister"); _record(ps2, "playerstart", {"tag": "Sister"})

ws = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_asset("/Game/Project_CX/Characters/Brother/BP_GameMode").generated_class())
ws.set_editor_property("kill_z", -1500.0)

les.save_current_level()
json.dump({"level": LEVEL, "tag": TAG, "actors": manifest}, open(MANIFEST, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
kinds = {}
for e in manifest:
    kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1
print("built", len(manifest), "actors", kinds)
