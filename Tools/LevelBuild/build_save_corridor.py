"""
세이브 복도 빌더 — .claude/Plans/세이브 복도_레벨디자인.md 의 "SERVICE CORRIDOR"(Map2 → 보스맵 사이 쉼터)를 Soul City 에셋으로 만든다.

좌표계 : +Y 진행(화면 오른쪽), 카메라는 -X 쪽에서 +X를 바라봄, Z 위 (Map1/Map2와 같음)
레인   : X ∈ [-120, 150] (Map2와 같은 폭). 레인 앞(X < -120)에는 발판 높이를 넘는 메시를 두지 않는다.
길이   : Y -400 ~ 3600 (40m). 바닥 0 → 계단(30°) → -180.
충돌   : 투명 COL_ 큐브(BlockAll, Camera 무시, 게임 중 숨김)만. 아트 메시는 전부 NoCollision.
조명   : 같은 스크립트(폴더 Lighting/*) — 차가운 형광등 + 세이브포인트 주변만 따뜻한 빛.

실행(에디터 원격 실행 또는 UnrealEditor-Cmd -run=pythonscript):
  1회차: 현재 레벨이 복도가 아니면 새로 만들거나(없을 때) 불러오고 끝낸다(로드는 자기 호출에서만 — 크래시 방지).
  2회차: 태그 GEN_SaveCorridor 액터만 지우고 다시 만든 뒤 저장한다(사람이 추가한 액터는 건드리지 않음).
  ※ 사람이 이 레벨을 손으로 고친 뒤에는 재실행하지 말 것(태그 달린 액터의 수정 내용이 사라진다).
"""
import unreal, json, math, random

LEVEL = "/Game/Project_CX/Maps/Map_SaveCorridor"
TAG = "GEN_SaveCorridor"
NEXT_MAP = "/Game/Project_CX/Maps/Map3_Final"
GAME_MODE_BP = "/Game/Project_CX/Characters/Brother/BP_GameMode"
CUBE = "/Engine/BasicShapes/Cube.Cube"
MAT_DIR = "/Game/Project_CX/Environment/Materials"

LANE_F, LANE_B = -120.0, 150.0      # 플레이 레인 앞/뒤
FRONT_X = -320.0                    # 바닥판이 보이는 앞 끝(발판 높이까지만 — 가림 없음)
WALL_X0, WALL_X1 = 150.0, 172.0     # 뒷벽(레인 바로 뒤)
Y_START, Y_END = -400.0, 3600.0
Z_HI, Z_LO = 0.0, -180.0            # 입구 층 / 복도 본층
STAIR_Y0, STAIR_Y1 = 700.0, 1012.0  # 계단(런 312 / 라이즈 180 = 30°)
SAVE_Y = 1750.0
WALL_TOP = 660.0                    # 뒷벽 윗면(입구 층 + 660 / 본층 + 840 — 계단 구간 시야 포함)
EXIT_Y = 3350.0

WARM = (1.0, 0.62, 0.3)
BULB = (1.0, 0.78, 0.5)
FLUO = (0.72, 0.88, 1.0)
COLD = (0.45, 0.6, 1.0)
RED = (1.0, 0.1, 0.06)
CYAN = (0.1, 0.85, 1.0)


def build():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    INV = {m["path"].split("/")[-1]: m for m in json.load(open(unreal.Paths.project_saved_dir() + "soul_inventory.json"))}
    manifest_path = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map_SaveCorridor_manifest.json")
    rng = random.Random(55)
    manifest = []
    cache = {}

    removed = 0
    for a in eas.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            removed += 1
    print("removed", removed, "old generated actors")

    def mesh(name):
        if name not in cache:
            cache[name] = unreal.load_asset(INV[name]["path"] if name in INV else name)
            if cache[name] is None:
                raise RuntimeError("mesh not found: " + name)
        return cache[name]

    def finish(a, folder, label):
        a.set_folder_path(folder)
        if label:
            a.set_actor_label(label)
        a.tags = [TAG]
        return a

    def record(a, kind, extra=None):
        l, r, s = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
        e = {"label": a.get_actor_label(), "kind": kind, "folder": str(a.get_folder_path()),
             "loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)], "rot": [round(r.pitch, 1), round(r.yaw, 1), round(r.roll, 1)],
             "scale": [round(s.x, 3), round(s.y, 3), round(s.z, 3)]}
        if extra:
            e.update(extra)
        manifest.append(e)

    # ------------------------------------------------------------ art helpers (Map1 방식: 바운드 중심 = (cx,cy), 바닥 = bottom_z)
    def ext(name, yaw, sc):
        s = INV[name]["size"]
        if round(yaw) % 180 == 90:
            return s[1] * sc[1], s[0] * sc[0], s[2] * sc[2]
        return s[0] * sc[0], s[1] * sc[1], s[2] * sc[2]

    def art(name, cx, cy, bottom_z, yaw=90.0, scale=1.0, folder="Art", label=None):
        m = INV[name]
        sc = tuple(scale) if isinstance(scale, (tuple, list)) else (scale, scale, scale)
        lcx = (m["min"][0] + m["max"][0]) / 2 * sc[0]
        lcy = (m["min"][1] + m["max"][1]) / 2 * sc[1]
        rad = math.radians(yaw)
        wx = lcx * math.cos(rad) - lcy * math.sin(rad)
        wy = lcx * math.sin(rad) + lcy * math.cos(rad)
        loc = unreal.Vector(cx - wx, cy - wy, bottom_z - m["min"][2] * sc[2])
        a = eas.spawn_actor_from_object(mesh(name), loc, unreal.Rotator(pitch=0, yaw=yaw, roll=0))
        a.set_actor_scale3d(unreal.Vector(*sc))
        comp = a.static_mesh_component
        comp.set_collision_profile_name("NoCollision")
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        finish(a, folder, label)
        ex, ey, ez = ext(name, yaw, sc)
        record(a, "art", {"mesh": name, "bbox": [round(v, 1) for v in (cx - ex / 2, cx + ex / 2, cy - ey / 2, cy + ey / 2, bottom_z, bottom_z + ez)]})
        return a

    def fit(name, x0, x1, y0, y1, z0, z1, yaw=90.0, folder="Art", label=None):
        """메시를 비균등 스케일해 박스를 정확히 채운다."""
        s = INV[name]["size"]
        wx, wy, wz = max(x1 - x0, 1), max(y1 - y0, 1), max(z1 - z0, 1)
        if round(yaw) % 180 == 90:
            sc = (wy / max(s[0], 1), wx / max(s[1], 1), wz / max(s[2], 1))
        else:
            sc = (wx / max(s[0], 1), wy / max(s[1], 1), wz / max(s[2], 1))
        return art(name, (x0 + x1) / 2, (y0 + y1) / 2, z0, yaw, sc, folder, label)

    def floor_span(names, x0, x1, y0, y1, top, thick, tile_x, tile_y, folder):
        """바닥판을 범위에 겹침 없이 정확히 채운다(타일 크기를 범위에 맞게 살짝 늘리거나 줄임)."""
        nx = max(1, round((x1 - x0) / tile_x))
        ny = max(1, math.ceil((y1 - y0) / tile_y - 0.01))
        w, d = (x1 - x0) / nx, (y1 - y0) / ny
        for ix in range(nx):
            for iy in range(ny):
                fit(rng.choice(names), x0 + w * ix, x0 + w * (ix + 1), y0 + d * iy, y0 + d * (iy + 1), top - thick, top, yaw=0, folder=folder)

    def wall_span(names, x0, x1, y0, y1, z0, z1, tile_y=300.0, tile_z=300.0, folder="Art", pick=None):
        """뒷벽 패널을 [y0,y1]x[z0,z1]에 겹침 없이 정확히 채운다. pick(iy, iz) -> 메시 이름(없으면 무작위)."""
        ny = max(1, math.ceil((y1 - y0) / tile_y - 0.25))
        nz = max(1, math.ceil((z1 - z0) / tile_z - 0.25))
        d, h = (y1 - y0) / ny, (z1 - z0) / nz
        for iy in range(ny):
            for iz in range(nz):
                n = (pick(iy, iz) if pick else None) or rng.choice(names)
                fit(n, x0, x1, y0 + d * iy, y0 + d * (iy + 1), z0 + h * iz, z0 + h * (iz + 1), yaw=90, folder=folder)

    def stairs_art(x0, x1, y0, z0, y1, z1, folder):
        """SM_stairs_W1_H3_00(런 312 / 라이즈 182). yaw 0이면 -Y 쪽으로 올라감 → +Y로 오르는 계단은 yaw 180(알려진 함정)."""
        rise = abs(z1 - z0)
        n = max(1, int(round((x1 - x0) / 183)))
        yaw = 180.0 if (z1 - z0) * (y1 - y0) > 0 else 0.0
        for i in range(n):
            fit("SM_stairs_W1_H3_00", x0 + (x1 - x0) * i / n, x0 + (x1 - x0) * (i + 1) / n, min(y0, y1), max(y0, y1),
                min(z0, z1), min(z0, z1) + rise, yaw=yaw, folder=folder)

    def under_stairs(x0, x1, y0, z0, y1, z1, base, folder, seg=78.0):
        """계단 옆(카메라 쪽)에서 보이는 빈 삼각형을 계단식 벽판으로 막는다(윗면 = 계단면 아래)."""
        n = max(1, int(abs(y1 - y0) // seg))
        for i in range(n):
            ya, yb = y0 + (y1 - y0) * i / n, y0 + (y1 - y0) * (i + 1) / n
            za, zb = z0 + (z1 - z0) * i / n, z0 + (z1 - z0) * (i + 1) / n
            h = min(za, zb) - base - 4
            if h > 10:
                fit("SM_Slums_Wall_02k", x0, x1, min(ya, yb), max(ya, yb), base, base + h, yaw=90, folder=folder)

    # ------------------------------------------------------------ collision helpers
    def col_setup(a, label, folder):
        comp = a.static_mesh_component
        comp.set_collision_profile_name("BlockAll")
        # 스프링암 충돌 테스트가 투명벽/지형에 걸려 카메라가 당겨지지 않도록
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
        comp.set_editor_property("visible", False)
        comp.set_editor_property("hidden_in_game", True)
        comp.set_editor_property("cast_shadow", False)
        finish(a, folder, "COL_" + label)

    def col(label, x0, x1, y0, y1, z0, z1, folder="Collision", kind="collision"):
        a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
        a.set_actor_scale3d(unreal.Vector((x1 - x0) / 100, (y1 - y0) / 100, (z1 - z0) / 100))
        col_setup(a, label, folder)
        record(a, kind, {"box": [x0, x1, y0, y1, z0, z1]})
        return a

    def ramp(label, x0, x1, y0, z0, y1, z1, thick=20, folder="Collision"):
        """윗면이 (y0,z0)→(y1,z1)인 경사 충돌."""
        dy, dz = y1 - y0, z1 - z0
        length = math.hypot(dy, dz)
        ang = math.degrees(math.atan2(dz, dy))
        ny, nz = -dz / length, dy / length
        a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2 - ny * thick / 2, (z0 + z1) / 2 - nz * thick / 2))
        a.set_actor_rotation(unreal.Rotator(pitch=ang, yaw=90, roll=0), False)
        a.set_actor_scale3d(unreal.Vector(length / 100, (x1 - x0) / 100, thick / 100))
        col_setup(a, label, folder)
        record(a, "collision_ramp", {"x": [x0, x1], "from": [y0, z0], "to": [y1, z1], "angle": round(abs(ang), 1)})
        return a

    def prop(name, cx, cy, floor, yaw=90.0, scale=1.0, folder="Art", block=True):
        """레인 안 소품 + (block이면) 바운드 그대로의 COL_Prop 박스(레인 밖은 잘라냄)."""
        a = art(name, cx, cy, floor, yaw, scale, folder)
        if block:
            b = manifest[-1]["bbox"]
            x0, x1 = max(b[0], LANE_F), min(b[1], LANE_B)
            if x1 - x0 > 5:
                col("Prop_" + a.get_actor_label(), x0, x1, b[2], b[3], floor, b[5], folder=folder.replace("Art/", "Collision/", 1), kind="blocker")
        return a

    # ------------------------------------------------------------ lighting helpers
    def color(rgb):
        return unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255)

    def point(label, x, y, z, rgb, cd=30.0, radius=700.0, shadows=False, folder="Lighting/Practical"):
        a = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z))
        c = a.point_light_component
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_editor_property("intensity", cd)
        c.set_editor_property("light_color", color(rgb))
        c.set_editor_property("attenuation_radius", radius)
        c.set_editor_property("cast_shadows", shadows)
        c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        finish(a, folder, "PL_" + label)
        record(a, "light")
        return a

    def neon(label, cx, cy, cz, length, mi, axis="Y", thick=6, folder="Lighting/Neon"):
        half_x = (length if axis == "X" else thick) / 2
        assert cx - half_x >= LANE_F, label   # 카메라 앞 금지
        a = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector(cx, cy, cz))
        s = [thick / 100] * 3
        s["XYZ".index(axis)] = length / 100
        a.set_actor_scale3d(unreal.Vector(*s))
        comp = a.static_mesh_component
        comp.set_material(0, unreal.load_asset(f"{MAT_DIR}/{mi}"))
        comp.set_collision_profile_name("NoCollision")
        comp.set_editor_property("cast_shadow", False)
        finish(a, folder, "NEON_" + label)
        record(a, "neon")
        return a

    def text(label, x, y, z, s, size, rgb, folder="Art/Story"):
        """벽에 쓴 글씨(TextRender, 카메라 쪽 -X를 향함)."""
        a = eas.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(x, y, z), unreal.Rotator(pitch=0, yaw=180, roll=0))
        c = a.get_editor_property("text_render")
        c.set_editor_property("text", s)
        c.set_editor_property("world_size", size)
        c.set_editor_property("text_render_color", color(rgb))
        c.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        c.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
        c.set_collision_profile_name("NoCollision")
        finish(a, folder, "TXT_" + label)
        record(a, "text", {"text": s})
        return a

    WALLS_CONC = ["SM_Slums_Wall_02a", "SM_Slums_Wall_02b", "SM_Slums_Wall_02e", "SM_Slums_Wall_02k"]
    WALLS_WIN = ["SM_Slums_Wall_02c", "SM_Slums_Wall_02d"]
    CRATES = ["SM_Slums_WoodWall_02a", "SM_Slums_WoodWall_02c", "SM_Slums_WoodWall_02d"]
    JUNK = ["SM_Slums_Trashcan_Closed02", "SM_Slums_Trashcan_Open01", "SM_Slums_Trashbag", "SM_Slums_Pipe_01a"]
    LOW_JUNK = ["SM_Slums_Trash_01a", "SM_Slums_Trash_02c", "SM_Slums_Trashcan_Lid", "SM_Slums_Pipe_05c"]

    # ================================================================ 경계(레인 앞/뒤, 양 끝 — 막힘)
    B = "Collision/Bounds"
    col("Bound_Front", LANE_F - 20, LANE_F, Y_START, Y_END, -400, 1200, B, kind="blocker")
    col("Bound_Back", LANE_B, LANE_B + 20, Y_START, Y_END, -400, 1200, B, kind="blocker")
    col("Bound_Start", -350, 400, Y_START - 20, Y_START, -400, 1200, B, kind="blocker")
    col("Bound_End", -350, 400, Y_END, Y_END + 20, -400, 1200, B, kind="blocker")

    # ================================================================ 지형 충돌
    C = "Collision/Floor"
    col("C0_Floor", LANE_F - 20, LANE_B + 20, Y_START, STAIR_Y0, -240, Z_HI, C)
    ramp("C1_Stair", LANE_F - 20, LANE_B + 20, STAIR_Y0, Z_HI, STAIR_Y1, Z_LO, folder=C)
    col("C1_StairBase", LANE_F - 20, LANE_B + 20, STAIR_Y0, STAIR_Y1, -240, Z_LO, C)
    col("C2_Floor", LANE_F - 20, LANE_B + 20, STAIR_Y1, Y_END, -240, Z_LO, C)

    # ================================================================ C0 입구 해치 (Y -400 ~ 700, 바닥 0)
    S = "C0_EntryHatch"
    floor_span(["SM_Slums_Floor_02a"], FRONT_X, LANE_B, Y_START, STAIR_Y0, Z_HI, 18, 200, 200, f"Art/{S}/Floor")
    fit("SM_Slums_Wall_02k", FRONT_X - 20, FRONT_X, Y_START, STAIR_Y0, -400, Z_HI - 18, yaw=90, folder=f"Art/{S}/FloorEdge")
    wall_span(WALLS_CONC, WALL_X0, WALL_X1, Y_START, STAIR_Y0, Z_HI, WALL_TOP, tile_z=330, folder=f"Art/{S}/BackWall")
    # 지나온 해치(닫힌 산업 셔터) — 시작 끝벽
    fit("SM_Slums_Wall_03a", FRONT_X, 450, Y_START - 64, Y_START, -400, WALL_TOP, yaw=0, folder=f"Art/{S}/Hatch")
    fit("SM_Slums_Wall_03b", -100, 140, Y_START - 4, Y_START + 16, Z_HI, 300, yaw=0, folder=f"Art/{S}/Hatch")
    neon("C0_HatchTop", 20, Y_START + 20, 305, 240, "MI_Neon_Cyan_12", axis="X", thick=5)
    # 벽 배관 + 환풍구 + 안내판
    art("SM_Slums_Wall_Pipe_01", WALL_X0 - 40, 150, Z_HI + 300, yaw=90, scale=(1.0, 0.6, 0.6), folder=f"Art/{S}/Pipes")
    art("SM_Slums_Vent_02b", WALL_X0 - 17, -150, Z_HI + 190, yaw=90, folder=f"Art/{S}/Vents")
    text("C0_ZoneSign", WALL_X0 - 3, 300, Z_HI + 230, "SERVICE CORRIDOR  D5-07", 30, (0.55, 0.85, 1.0))
    text("C0_ZoneSub", WALL_X0 - 3, 300, Z_HI + 200, "MAINTENANCE ACCESS // PIT LEVEL BELOW", 14, (0.5, 0.65, 0.8))
    prop("SM_Slums_WoodWall_02c", 95, -250, Z_HI, yaw=90, folder=f"Art/{S}/Props")
    prop("SM_Slums_Trashcan_Closed02", 110, 480, Z_HI, yaw=20, folder=f"Art/{S}/Props")
    art("SM_Slums_Trash_01a", 90, 560, Z_HI, yaw=70, folder=f"Art/{S}/Props")

    # ================================================================ C1 정비 계단 (Y 700 ~ 1012, 0 → -180)
    S = "C1_ServiceStairs"
    stairs_art(FRONT_X, LANE_B, STAIR_Y0, Z_HI, STAIR_Y1, Z_LO, folder=f"Art/{S}/Stairs")
    under_stairs(FRONT_X - 20, FRONT_X, STAIR_Y0, Z_HI, STAIR_Y1, Z_LO, -400, folder=f"Art/{S}/Side")
    fit("SM_Slums_Wall_02k", FRONT_X - 20, FRONT_X, STAIR_Y0, STAIR_Y1, -400, Z_LO - 4, yaw=90, folder=f"Art/{S}/Side")
    wall_span(WALLS_CONC, WALL_X0, WALL_X1, STAIR_Y0, STAIR_Y1, Z_LO, WALL_TOP, tile_z=280, folder=f"Art/{S}/BackWall")
    neon("C1_StepEdge", 15, STAIR_Y0 + 4, Z_HI + 2, 260, "MI_Neon_Orange", axis="X", thick=3)   # 첫 계단 가장자리 안내선
    neon("C1_Handrail", WALL_X0 - 8, (STAIR_Y0 + STAIR_Y1) / 2, (Z_HI + Z_LO) / 2 + 100, 360, "MI_Neon_Cyan_12", thick=4)

    # ================================================================ C2 세이브 쉼터 (Y 1012 ~ 2500, 바닥 -180)
    S = "C2_SaveBay"
    floor_span(["SM_Slums_Floor_02a"], FRONT_X, LANE_B, STAIR_Y1, 1500, Z_LO, 18, 200, 200, f"Art/{S}/Floor")
    floor_span(["SM_Slums_Floor_01a", "SM_Slums_Floor_01c", "SM_Slums_Floor_01e"], FRONT_X, LANE_B, 1500, 2500, Z_LO, 20, 260, 200, f"Art/{S}/Floor")
    fit("SM_Slums_Wall_02k", FRONT_X - 20, FRONT_X, STAIR_Y1, 2500, -400, Z_LO - 18, yaw=90, folder=f"Art/{S}/FloorEdge")
    # 뒷벽: 가운데 층에 창문 2칸(불 켜진 관리실)
    win_cells = {(1, 1), (2, 1)}
    wall_span(WALLS_CONC, WALL_X0, WALL_X1, STAIR_Y1, 2500, Z_LO, WALL_TOP, tile_z=280, folder=f"Art/{S}/BackWall",
              pick=lambda iy, iz: rng.choice(WALLS_WIN) if (iy, iz) in win_cells else None)
    art("SM_Slums_Wall_Pipe_01", WALL_X0 - 40, 1300, Z_LO + 330, yaw=90, scale=(1.0, 0.6, 0.6), folder=f"Art/{S}/Pipes")
    art("SM_Slums_Wall_Pipe_01", WALL_X0 - 40, 2200, Z_LO + 330, yaw=90, scale=(1.0, 0.6, 0.6), folder=f"Art/{S}/Pipes")
    # 쉼터 흔적: 누가 여기서 지냈다 — 상자 탁자, 쓰레기봉투, 벽에 건 천, 낙서
    prop("SM_Slums_WoodWall_02c", 95, 2150, Z_LO, yaw=90, folder=f"Art/{S}/Camp")
    prop("SM_Slums_WoodWall_02b", 105, 2290, Z_LO, yaw=90, folder=f"Art/{S}/Camp")
    prop("SM_Slums_Trashbag", 115, 2010, Z_LO, yaw=40, folder=f"Art/{S}/Camp")
    art("SM_Slums_Light_01a_OP", 95, 2150, Z_LO + 114, yaw=0, folder=f"Art/{S}/Camp")
    art("SM_Slums_Cloth_01a", WALL_X0 - 15, 2380, Z_LO + 60, yaw=90, scale=0.8, folder=f"Art/{S}/Camp")
    text("C2_Graffiti", WALL_X0 - 3, 2150, Z_LO + 250, "KEEP THE LIGHT ON", 34, (1.0, 0.7, 0.35))
    text("C2_Tally", WALL_X0 - 3, 2150, Z_LO + 215, "|||| |||| |||| ||", 20, (0.85, 0.8, 0.7))
    text("C2_Names", WALL_X0 - 3, 1980, Z_LO + 160, "J + M  WERE HERE", 14, (0.8, 0.75, 0.65))
    for yy in (1250, 2450):
        art(rng.choice(LOW_JUNK), 120, yy, Z_LO, yaw=rng.uniform(0, 360), folder=f"Art/{S}/Props")

    # ================================================================ C3 피트 출입구 (Y 2500 ~ 3600, 바닥 -180)
    S = "C3_PitAccess"
    floor_span(["SM_Slums_Floor_02a"], FRONT_X, LANE_B, 2500, Y_END, Z_LO, 18, 200, 200, f"Art/{S}/Floor")
    fit("SM_Slums_Wall_02k", FRONT_X - 20, FRONT_X, 2500, Y_END, -400, Z_LO - 18, yaw=90, folder=f"Art/{S}/FloorEdge")
    wall_span(WALLS_CONC, WALL_X0, WALL_X1, 2500, Y_END, Z_LO, WALL_TOP, tile_z=280, folder=f"Art/{S}/BackWall")
    art("SM_Slums_Wall_Pipe_01", WALL_X0 - 40, 2800, Z_LO + 330, yaw=90, scale=(1.0, 0.6, 0.6), folder=f"Art/{S}/Pipes")
    prop("SM_Slums_Pipes_04c", WALL_X0 - 25, 2580, Z_LO, yaw=0, folder=f"Art/{S}/Pipes")
    art("SM_Slums_Vent_02c", WALL_X0 - 17, 2680, Z_LO + 190, yaw=90, folder=f"Art/{S}/Vents")
    text("C3_Warning", WALL_X0 - 3, 2900, Z_LO + 270, "PIT ACCESS  >>", 30, (1.0, 0.35, 0.2))
    text("C3_WarningSub", WALL_X0 - 3, 2900, Z_LO + 240, "AUTHORIZED UNITS ONLY // NO RETURN PAST THIS POINT", 13, (0.9, 0.55, 0.45))
    prop("SM_Slums_Pipe_01a", 110, 3040, Z_LO, yaw=0, folder=f"Art/{S}/Props")
    prop("SM_Slums_Trashcan_Open01", 115, 2700, Z_LO, yaw=200, folder=f"Art/{S}/Props")
    # 끝: 닫힌 방폭문(보스맵 입구) — 출구 액터의 네온 프레임이 그 앞에 선다
    fit("SM_Slums_Wall_03a", FRONT_X, 450, Y_END, Y_END + 64, -400, WALL_TOP, yaw=0, folder=f"Art/{S}/BlastDoor")
    fit("SM_Slums_Wall_03b", -100, 140, Y_END - 16, Y_END + 4, Z_LO, Z_LO + 330, yaw=0, folder=f"Art/{S}/BlastDoor")
    for i, yy in enumerate(range(3150, 3560, 100)):   # 바닥 경고 줄무늬(레인 앞쪽 가장자리, 바닥 높이)
        neon(f"C3_Hazard_{i}", -100, yy, Z_LO + 1, 40, "MI_Neon_Orange", axis="X", thick=3)

    # ================================================================ 끝 너머 막힌 덩어리(카메라가 양 끝에서 좌우 ~9m를 보므로 빈 공간 대신 콘크리트 면)
    # 레인 밖(Y 범위 밖)이라 레인을 가리지 않는다. 정면(X = 바닥 앞 끝)만 보이면 된다.
    for k, (ya, yb) in enumerate(((Y_START - 1400, Y_START - 64), (Y_END + 64, Y_END + 1400))):
        wall_span(WALLS_CONC, FRONT_X - 30, FRONT_X - 10, ya, yb, -400, WALL_TOP, tile_y=300, tile_z=355, folder=f"Art/EndMass_{k}")
        fit("SM_Slums_Floor_02a", FRONT_X - 10, WALL_X1, ya, yb, WALL_TOP - 10, WALL_TOP, yaw=0, folder=f"Art/EndMass_{k}")   # 윗면 뚜껑

    # ================================================================ 천장 보(머리 위 — 복도 느낌, 레인 위에만)
    for i, yy in enumerate((-100, 450, 1300, 1900, 2600, 3200)):
        floor_z = Z_HI if yy < STAIR_Y0 else Z_LO
        fit("SM_Slums_Wall_02k", LANE_F, WALL_X1, yy - 15, yy + 15, floor_z + 430, floor_z + 470, yaw=90, folder="Art/Ceiling/Beams")

    # ================================================================ 조명: 차가운 기본광 + 형광등, 세이브포인트만 따뜻하게
    ATM = "Lighting/Atmosphere"
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 1600, 2000))
    sc = sky.light_component
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sc.set_editor_property("intensity", 0.8)
    sc.set_editor_property("light_color", color((0.4, 0.52, 0.8)))
    cube = unreal.load_asset("/Engine/MapTemplates/Sky/SunsetAmbientCubemap")
    if cube:
        sc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sc.set_editor_property("cubemap", cube)
    finish(sky, ATM, "SkyLight_Corridor"); record(sky, "skylight")

    key = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 1600, 2500), unreal.Rotator(pitch=-48, yaw=25, roll=0))
    kc = key.light_component
    kc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    kc.set_editor_property("intensity", 1.2)
    kc.set_editor_property("light_color", color((0.55, 0.65, 1.0)))
    kc.set_editor_property("cast_shadows", True)
    finish(key, ATM, "DirLight_Fill"); record(key, "dirlight")

    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 1600, -400))
    fc = fog.component
    fc.set_editor_property("fog_density", 0.015)
    fc.set_editor_property("fog_height_falloff", 0.06)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.015, 0.025, 0.05, 1.0))
    fc.set_editor_property("start_distance", 1200.0)
    fc.set_editor_property("enable_volumetric_fog", True)
    fc.set_editor_property("volumetric_fog_extinction_scale", 1.0)
    finish(fog, ATM, "Fog_Corridor"); record(fog, "fog")

    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 1600, 0))
    ppv.set_editor_property("unbound", True)
    s = ppv.settings
    for k, v in [("auto_exposure_min_brightness", -0.5), ("auto_exposure_max_brightness", 1.0), ("auto_exposure_bias", 0.4),
                 ("bloom_intensity", 0.8), ("vignette_intensity", 0.5)]:
        s.set_editor_property("override_" + k, True)
        s.set_editor_property(k, v)
    s.set_editor_property("override_color_saturation", True)
    s.set_editor_property("color_saturation", unreal.Vector4(0.95, 1.0, 1.1, 1.0))
    ppv.set_editor_property("settings", s)
    finish(ppv, ATM, "PPV_Corridor"); record(ppv, "ppv")

    L = "Lighting/Practical"
    # 형광등(차가운 백청색): 뒷벽 위쪽 튜브 + 포인트 라이트. 세이브 쉼터 주변은 비워 따뜻한 빛이 돋보이게.
    for i, (yy, fz) in enumerate([(0, Z_HI), (550, Z_HI), (1200, Z_LO), (2750, Z_LO), (3250, Z_LO)]):
        neon(f"Fluo_{i}", WALL_X0 - 10, yy, fz + 300, 120, "MI_Neon_Cyan_12", thick=5)
        point(f"Fluo_{i}", 40, yy, fz + 290, FLUO, cd=22.0, radius=650.0, shadows=(i % 2 == 0), folder=L)
    point("C0_HatchGlow", 20, Y_START + 120, Z_HI + 260, CYAN, cd=12.0, radius=500.0, folder=L)
    point("C1_StairFill", 40, 860, Z_HI + 160, FLUO, cd=15.0, radius=500.0, folder=L)
    # 세이브 쉼터: 따뜻한 빛(세이브포인트 자체 상태등과 별도), 캠프 전구, 창문 너머 관리실
    point("C2_SaveWarm", -10, SAVE_Y, Z_LO + 230, WARM, cd=45.0, radius=800.0, shadows=True, folder=L)
    point("C2_CampBulb", 70, 2180, Z_LO + 200, BULB, cd=25.0, radius=550.0, shadows=True, folder=L)
    point("C2_OfficeWindow", 260, 1500, Z_LO + 360, WARM, cd=60.0, radius=700.0, folder=L)
    # 출구: 붉은 경고등 + 문 위 청록
    point("C3_WarnA", 100, 3050, Z_LO + 330, RED, cd=20.0, radius=500.0, folder=L)
    point("C3_WarnB", 100, 3520, Z_LO + 360, RED, cd=25.0, radius=600.0, folder=L)
    point("C3_DoorCyan", 0, Y_END - 150, Z_LO + 260, CYAN, cd=30.0, radius=800.0, shadows=True, folder=L)

    # camera_cull.py는 쓰지 않는다: PIE 실측(2026-11-04) 결과 현재 분할 화면은 캐릭터 깊이에서 가로 약 18m가 보여
    # (카메라 거리 ≈ 2700) camera_cull의 카메라 모델(1289/433)보다 훨씬 넓다 → 컬링하면 뒷벽 윗단이 지워져 빈 하늘이 보였다.
    # 대신 뒷벽 높이를 "바닥 + 660"으로 제한(실측 화면 위 끝 ≈ 바닥 + 550 + 점프 90)해 보이는 만큼만 쌓는다.

    # ================================================================ 게임플레이: 시작 지점 2개, 세이브포인트, 출구
    G = "Gameplay"
    ps1 = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-20, 0, Z_HI + 92))
    finish(ps1, f"{G}/PlayerStarts", "PlayerStart_Brother"); record(ps1, "playerstart")
    ps2 = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-20, -200, Z_HI + 82))
    ps2.set_editor_property("player_start_tag", "Sister")
    finish(ps2, f"{G}/PlayerStarts", "PlayerStart_Sister"); record(ps2, "playerstart", {"tag": "Sister"})

    sp = eas.spawn_actor_from_class(unreal.CXSavePoint, unreal.Vector(40, SAVE_Y, Z_LO))
    sp.set_editor_property("save_point_id", "SP_Corridor_01")
    sp.set_editor_property("zone_name", "SERVICE CORRIDOR")
    sp.set_editor_property("zone_extent", unreal.Vector(135.0, 170.0, 160.0))   # X = 레인 반폭(중심 X 40 → -95 ~ 175)
    sp.set_editor_property("spawn_depth", -40.0)                                  # 스폰 X = 0
    finish(sp, f"{G}/Flow", "CXSavePoint_Corridor_01"); record(sp, "savepoint", {"id": "SP_Corridor_01"})
    # 단말기 기둥(로컬 X 30~80 → 월드 70~120)은 몸이 통과하지 않게
    col("Prop_SavePointTerminal", 65, 125, SAVE_Y - 40, SAVE_Y + 40, Z_LO, Z_LO + 240, folder=f"Collision/{G}", kind="blocker")

    ex = eas.spawn_actor_from_class(unreal.CXLevelExit, unreal.Vector(15, EXIT_Y, Z_LO))
    ex.set_next_map_package(NEXT_MAP)
    ex.set_editor_property("zone_extent", unreal.Vector(135.0, 200.0, 180.0))    # Map2 출구와 같은 레인 폭
    ex.set_editor_property("show_frame", True)
    ex.set_editor_property("frame_depth", 120.0)
    finish(ex, f"{G}/Flow", "CXLevelExit_Corridor_ToMap3Final"); record(ex, "levelexit", {"next": NEXT_MAP})

    ws = world.get_world_settings()
    ws.set_editor_property("default_game_mode", unreal.load_asset(GAME_MODE_BP).generated_class())
    ws.set_editor_property("kill_z", -1500.0)

    les.save_current_level()
    json.dump({"level": LEVEL, "tag": TAG, "actors": manifest}, open(manifest_path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    kinds = {}
    for e in manifest:
        kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1
    print("built", len(manifest), "actors", kinds, "->", manifest_path)


def run():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    cur = les.get_current_level().get_outermost().get_name() if les.get_current_level() else ""
    if cur != LEVEL:
        unreal.SystemLibrary.collect_garbage()
        if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
            print("load_level:", les.load_level(LEVEL), "— run this script again to build")
        else:
            print("new_level:", les.new_level(LEVEL), "— run this script again to build")
        return
    build()


run()
