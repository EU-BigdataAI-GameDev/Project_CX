"""
Map1/Map2 벽 구멍 메우기 (2026-11-04, 추가 전용).

- 사람이 다듬은 레벨이라 기존 액터는 **옮기지도 지우지도 않는다**. 이 스크립트가 만든 액터(태그 `GEN_WallFill`)만 지우고 다시 만든다(재실행 안전).
- 메운 조각은 이웃 벽 조각의 메시/머티리얼/회전/스케일을 그대로 복사해(ref) 같은 깊이(X)에 이어 붙인다. 라벨 `FILL_*`, 폴더 `Art/<Section>/WallFill`.
- 장식 메시는 NoCollision(프로젝트 규칙). 레인 경계에 걸리는 구멍이면 `COL_Fill_*` 박스(BlockAll, Camera 무시, 게임 중 숨김)를 함께 둔다.
- 구멍 찾기: PIE 실제 카메라(위아래 분할, 피치 -10, FOV 기준 화면 폭 약 19m)로 레인을 따라 캡처 → 하늘/빈 공간 색 픽셀을 벽 평면(X)에 역투영해서 찾았다.
- 결과 기록: `Docs/LevelManifests/WallFill_<Map>.json`
- v2(같은 날 오후): 구멍 단위 메우기 대신 레인 뒤 건물 열 바로 뒤에 세이브 복도 같은 콘크리트 뒷벽(SPAN, `FILL_*_BackWall*`)을 이어 세웠다.
  검사: PIE에서 원경을 숨기고 그 뒤에 발광 판을 둔 캡처 / 숨기기만 한 캡처를 비교해 "뒤가 비쳐 보이는" 픽셀을 찾는다(빈 하늘·원경이 보이는 틈 = 0 목표).

사용: 대상 맵을 에디터에서 연 상태로 이 파일을 에디터 Python으로 실행(열린 맵 이름으로 목록을 고른다). 저장은 직접(또는 SAVE=True).
"""
import unreal, json, math, random

TAG = "GEN_WallFill"
SAVE = False

# ---------------------------------------------------------------------------------------------
# v2 (2026-11-04 오후): "복도맵처럼 벽쪽 건물이 빈 부분 없이" — 레인 뒤 판잣집 열 바로 뒤에 세이브 복도와 같은
# 콘크리트 벽판(SM_Slums_Wall_02*)으로 이어진 뒷벽(backstop)을 세운다. 판잣집 사이/위/아래 틈으로 원경·빈 하늘 대신
# 건물 벽이 보이게 한다. span = 박스를 타일로 겹침 없이 채움(fit, 비균등 스케일, yaw 90 → 카메라(-X) 쪽을 봄).
# 높이: 카메라 위쪽 끝 광선은 거의 수평(피치 -10, 세로 반각 ≈10.6°) → 보이는 위 끝 ≈ 카메라 높이(발판 +435) + 점프 90 + 여유.
#       그보다 높게 쌓지 않는다. 아래 끝은 PIE에서 잰 가장 낮은 빈 공간 아래까지.
# ---------------------------------------------------------------------------------------------
WALL_DIR = "/Game/SoulCity/Environment/Meshes/Building_Slum/"
CONC = ["SM_Slums_Wall_02a", "SM_Slums_Wall_02b", "SM_Slums_Wall_02e", "SM_Slums_Wall_02k"]   # 복도 뒷벽과 같은 조합
WIN = ["SM_Slums_Wall_02c", "SM_Slums_Wall_02d"]                                              # 창문 벽판(복도 관리실 창)


def SPAN(prefix, folder, box, tile, seed, win_rate=0.22, meshes=None):
    """box = (x0, x1, y0, y1, z0, z1)를 tile = (폭 Y, 높이 Z) 근처 크기 타일로 정확히 채우는 조각 목록."""
    x0, x1, y0, y1, z0, z1 = box
    ny = max(1, round((y1 - y0) / tile[0])); nz = max(1, round((z1 - z0) / tile[1]))
    d, h = (y1 - y0) / ny, (z1 - z0) / nz
    rng = random.Random(seed); out = []
    for iy in range(ny):
        for iz in range(nz):
            m = rng.choice(WIN) if (iz > 0 and rng.random() < win_rate) else rng.choice(meshes or CONC)
            out.append(dict(label=f"{prefix}_{iy:02d}{iz}", mesh=m, folder=folder,
                            fit=(x0, x1, y0 + d * iy, y0 + d * (iy + 1), z0 + h * iz, z0 + h * (iz + 1))))
    return out

# center = 목표 바운드 중심(월드). 메시 피벗은 바운드 중심(Soul City 피벗 중심화)이지만, 스폰 후 실제 바운드로 한 번 더 맞춘다.
# ref = 메시/머티리얼을 복사할 이웃 액터 라벨. scale/yaw 를 생략하면 ref 값을 쓴다.
# Map1 v2 뒷벽: 판잣집 열(X 300~560) 바로 뒤 X 575~600. S3는 타워(Vent X 625~1576)를 가리지 않게 먼 판잣집 열(X 1500~1760) 뒤 X 1780~1805.
# S4는 기존 산업 벽(Wall_03a, X 420~484, Z -539~1336)이 이미 막혀 있어 뒤 S3 벽을 Y 9250까지만 늘려 경계 틈을 막는다.
# S3 벽은 왼쪽으로 Y 4900까지(S2 벽 끝 Y 6250 너머를 S3 위쪽 카메라가 비스듬히 본다 — S2 벽 뒤라 평소엔 가려짐).
MAP1_V2 = (
    SPAN("FILL_S1_BackWall_A", "Art/S1_ScrapStreet/WallFill", (575, 600, -700, 1600, -450, 720), (575, 585), 101)
    + SPAN("FILL_S1_BackWall_B", "Art/S1_ScrapStreet/WallFill", (575, 600, 1600, 3000, -450, 860), (575, 655), 102)
    + SPAN("FILL_S2_BackWall", "Art/S2_DroneWatch/WallFill", (575, 600, 3000, 6250, -450, 860), (575, 655), 103)
    + SPAN("FILL_S3_BackWall", "Art/S3_TowerExterior/WallFill", (1780, 1805, 4900, 9250, -800, 1350), (725, 720), 104)
    + SPAN("FILL_S5_BackWall_A", "Art/S5_Escape/WallFill", (575, 600, 9950, 11900, -450, 1350), (650, 600), 105)
    + SPAN("FILL_S5_BackWall_B", "Art/S5_Escape/WallFill", (575, 600, 11900, 12700, -450, 1050), (400, 500), 106)
    # 끝벽 너머 산업 벽판(FILL_S5_EndBackdrop, 위 끝 624) 위로 E4 발판(250)에서 하늘이 보인다 → 그 뒤에 윗단만
    + SPAN("FILL_S5_BackWall_C", "Art/S5_Escape/WallFill", (600, 625, 12700, 13400, 560, 1050), (700, 490), 107)
)

# Map2 v2 뒷벽: 판잣집/배경 열(X 166~642) 바로 뒤 X 660~685. S3 공동주택은 실내 뒷벽(X 150~185)이 막지만 지붕(1092) 위와
# S2/S4에서 비스듬히 보이는 틈 때문에 같은 높이로 이어 간다. S5 동굴은 바위(X 250~1305) 사이 틈 뒤 X 1320~1345에 같은 벽.
MAP2_V2 = (
    SPAN("FILL_S1_BackWall_A", "Art/S1_FloodedCanal/WallFill", (660, 685, -800, 1000, -600, 760), (600, 680), 201)
    + SPAN("FILL_S1_BackWall_B", "Art/S1_FloodedCanal/WallFill", (660, 685, 1000, 2400, -600, 1050), (600, 550), 202)
    + SPAN("FILL_S2_BackWall_A", "Art/S2_Split/WallFill", (660, 685, 2400, 4600, -400, 1050), (600, 725), 203)
    + SPAN("FILL_S2_BackWall_B", "Art/S2_Split/WallFill", (660, 685, 4600, 6000, -400, 1400), (600, 600), 204)
    + SPAN("FILL_S3_BackWall", "Art/S3_Tenement/WallFill", (660, 685, 6000, 9400, -400, 1400), (600, 600), 205)
    + SPAN("FILL_S4_BackWall", "Art/S4_RooftopMarket/WallFill", (660, 685, 9400, 11400, -400, 1450), (600, 620), 206)
    # S4 벽 끝(11400) 너머·출구 게이트 너머를 비스듬히 보는 시선까지 막도록 Y 10600~15000(양 끝은 앞 벽 뒤라 평소엔 가려짐)
    + SPAN("FILL_S5_BackWall", "Art/S5_CaveDescent/WallFill", (1320, 1345, 10600, 15000, -800, 1300), (730, 700), 207)
    # 시작 끝벽 너머 산업 벽판(FILL_S1_StartBackdrop, 아래 끝 -300) 밑으로 보이는 띠
    + SPAN("FILL_S0_StartBackdropLow", "Art/S1_FloodedCanal/WallFill", (775, 800, -1700, -780, -700, -250), (460, 450), 208, win_rate=0)
)

FILLS = {
    "Map1_SoulCity1": [
        # 시작 끝벽(Y -680) 너머: 카메라가 끝벽 앞 모서리를 돌아 빈 하늘을 본다 → 같은 산업 벽판으로 뒤를 막는다
        dict(label="FILL_S1_StartBackdrop", ref="SM_Slums_Wall_03a", folder="Art/S1_ScrapStreet/WallFill",
             center=(560, -1325, 151.75), yaw=90, scale=(1.125, 1.0, 1.5)),
        # S2 계단 뒤: 판잣집이 테라스 높이(182)에 떠 있어 그 아래와 S1/S2 판잣집 사이 틈으로 원경이 보인다 → 테라스 앞벽과 같은 판자벽
        dict(label="FILL_S2_StairBack_A", ref="SM_Slums_Wall_01a", folder="Art/S2_DroneWatch/WallFill",
             center=(340, 3080, 30.5), yaw=90),
        dict(label="FILL_S2_StairBack_B", ref="SM_Slums_Wall_01a", folder="Art/S2_DroneWatch/WallFill",
             center=(340, 3380, 30.5), yaw=90),
        dict(label="FILL_S2_StairBack_Gap", ref="SM_Slums_Wall_01a", folder="Art/S2_DroneWatch/WallFill",
             center=(345, 2966, 308.5), yaw=90),
        # S2→S3 경계: 먼 판잣집 열(X 1500~1760)이 Y 6398에서 시작해 그 앞쪽으로 하늘이 보인다 → 같은 열을 한 칸 앞으로 이어 붙인다
        dict(label="FILL_S3_FarRow_Mid", ref="SM_Slums_Distance_02e7", folder="Art/S3_TowerExterior/WallFill",
             center=(1610.5, 6213, 517.5)),
        dict(label="FILL_S3_FarRow_Top", ref="SM_Slums_Distance_01c9", folder="Art/S3_TowerExterior/WallFill",
             center=(1671, 6215, 979)),
        # S4 코어 플랫폼 뒤: 뒷벽(Wall_03a, X 420~484)이 플랫폼 높이(711)부터만 있어 그 아래가 비어 S3·S5에서 원경/빈 공간이 보인다
        # → 같은 벽판(같은 스케일)을 아래로 두 단 이어 붙인다
        dict(label="FILL_S4_BackLower_A1", ref="SM_Slums_Wall_03a3", folder="Art/S4_CoreHack/WallFill", center=(452, 8659, 398.5)),
        dict(label="FILL_S4_BackLower_A2", ref="SM_Slums_Wall_03a3", folder="Art/S4_CoreHack/WallFill", center=(452, 8659, -226.5)),
        dict(label="FILL_S4_BackLower_B1", ref="SM_Slums_Wall_03a5", folder="Art/S4_CoreHack/WallFill", center=(452, 9553, 398.5)),
        dict(label="FILL_S4_BackLower_B2", ref="SM_Slums_Wall_03a5", folder="Art/S4_CoreHack/WallFill", center=(452, 9553, -226.5)),
        # 끝벽(Y 12880) 너머 + 출구 게이트와 끝벽 사이 틈(Y 12699~12848): 시작 쪽과 같은 산업 벽판
        dict(label="FILL_S5_EndBackdrop", ref="SM_Slums_Wall_03a7", folder="Art/S5_Escape/WallFill",
             center=(560, 13425, 151.75), yaw=90, scale=(1.29, 1.0, 1.5)),
    ] + MAP1_V2,
    "Map2_SoulCity2": [
        # 시작 끝벽(Y -830, X -120~700) 너머 빈 하늘 → 끝벽과 같은 산업 벽판(같은 Z 스케일 1.43)을 끝벽 뒤에 ㄱ자로 붙인다
        dict(label="FILL_S1_StartBackdrop", ref="SM_Slums_Wall_03a", folder="Art/S1_FloodedCanal/WallFill",
             center=(740, -1380, 149.75), yaw=90, scale=(1.0, 0.94, 1.43)),
        # 출구 게이트 벽(Y 14232) 너머 빈 하늘 → 같은 방식
        dict(label="FILL_S5_EndBackdrop", ref="SM_Slums_Wall_03a2", folder="Art/S5_CaveDescent/WallFill",
             center=(740, 14940, 83.0), yaw=90, scale=(1.1667, 0.94, 1.6)),
    ] + MAP2_V2,
}


def _ues():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _find(label):
    for a in _eas().get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def _remove_own():
    n = 0
    for a in _eas().get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            _eas().destroy_actor(a)
            n += 1
    return n


def _place_art(f):
    ref = _find(f["ref"])
    if ref is None or not isinstance(ref, unreal.StaticMeshActor):
        raise RuntimeError("ref not found: " + f["ref"])
    rc = ref.static_mesh_component
    mesh = rc.static_mesh
    rr = ref.get_actor_rotation()
    yaw = f.get("yaw", rr.yaw)
    rot = unreal.Rotator(pitch=rr.pitch, yaw=yaw, roll=rr.roll)
    s = f.get("scale")
    scale = unreal.Vector(*s) if s else ref.get_actor_scale3d()
    cx, cy, cz = f["center"]
    a = _eas().spawn_actor_from_object(mesh, unreal.Vector(cx, cy, cz), rot)
    a.set_actor_scale3d(scale)
    a.set_actor_rotation(rot, False)
    c = a.static_mesh_component
    c.set_collision_profile_name("NoCollision")
    c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    for i, m in enumerate(rc.get_materials()):
        if m:
            c.set_material(i, m)
    o, e = a.get_actor_bounds(False)
    l = a.get_actor_location()
    a.set_actor_location(unreal.Vector(l.x + cx - o.x, l.y + cy - o.y, l.z + cz - o.z), False, False)
    a.set_folder_path(f["folder"])
    a.set_actor_label(f["label"])
    a.tags = [TAG]
    return a


_MESH = {}


def _place_fit(f):
    """f["fit"] 박스를 메시(yaw 90)로 정확히 채운다. 메시 로컬 X → 월드 Y, 로컬 Y → 월드 X."""
    name = f["mesh"]
    if name not in _MESH:
        _MESH[name] = unreal.load_asset(WALL_DIR + name)
        if _MESH[name] is None:
            raise RuntimeError("mesh not found: " + name)
    mesh = _MESH[name]
    x0, x1, y0, y1, z0, z1 = f["fit"]
    bb = mesh.get_bounding_box()
    sx, sy, sz = bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z
    rot = unreal.Rotator(pitch=0, yaw=90, roll=0)
    cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
    a = _eas().spawn_actor_from_object(mesh, unreal.Vector(cx, cy, cz), rot)
    a.set_actor_scale3d(unreal.Vector((y1 - y0) / sx, (x1 - x0) / sy, (z1 - z0) / sz))
    a.set_actor_rotation(rot, False)
    c = a.static_mesh_component
    c.set_collision_profile_name("NoCollision")
    c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    o, e = a.get_actor_bounds(False)
    l = a.get_actor_location()
    a.set_actor_location(unreal.Vector(l.x + cx - o.x, l.y + cy - o.y, l.z + cz - o.z), False, False)
    a.set_folder_path(f["folder"])
    a.set_actor_label(f["label"])
    a.tags = [TAG]
    return a


def _place_col(f):
    x0, x1, y0, y1, z0, z1 = f["box"]
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    a = _eas().spawn_actor_from_object(cube, unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    a.set_actor_scale3d(unreal.Vector((x1 - x0) / 100, (y1 - y0) / 100, (z1 - z0) / 100))
    c = a.static_mesh_component
    c.set_collision_profile_name("BlockAll")
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property("visible", False)
    c.set_editor_property("hidden_in_game", True)
    c.set_editor_property("cast_shadow", False)
    a.set_folder_path(f["folder"])
    a.set_actor_label(f["label"])
    a.tags = [TAG]
    return a


def run():
    world = _ues().get_editor_world()
    name = world.get_name()
    fills = FILLS.get(name)
    if fills is None:
        print("fill_wall_holes: no entry for", name)
        return
    removed = _remove_own()
    out = []
    for f in fills:
        a = _place_col(f) if "box" in f else _place_fit(f) if "fit" in f else _place_art(f)
        l = a.get_actor_location(); r = a.get_actor_rotation(); s = a.get_actor_scale3d()
        o, e = a.get_actor_bounds(False)
        out.append({
            "label": a.get_actor_label(), "folder": str(a.get_folder_path()),
            "kind": "collision" if "box" in f else "art",
            "mesh": a.static_mesh_component.static_mesh.get_path_name(), "ref": f.get("ref"), "v": 2 if "fit" in f else 1,
            "loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)], "rot": [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)],
            "scale": [round(s.x, 4), round(s.y, 4), round(s.z, 4)],
            "bounds": [round(o.x - e.x), round(o.x + e.x), round(o.y - e.y), round(o.y + e.y), round(o.z - e.z), round(o.z + e.z)],
        })
    mdir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/")
    short = "Map1" if name.startswith("Map1") else "Map2" if name.startswith("Map2") else name
    json.dump({"level": world.get_path_name(), "tag": TAG, "note": "add-only wall hole fills; remove = delete actors tagged " + TAG,
               "actors": out}, open(mdir + "WallFill_" + short + ".json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("fill_wall_holes:", name, "removed", removed, "placed", len(out))
    if SAVE:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


run()
del run
