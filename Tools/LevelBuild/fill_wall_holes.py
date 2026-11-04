"""
Map1/Map2 벽 구멍 메우기 (2026-11-04, 추가 전용).

- 사람이 다듬은 레벨이라 기존 액터는 **옮기지도 지우지도 않는다**. 이 스크립트가 만든 액터(태그 `GEN_WallFill`)만 지우고 다시 만든다(재실행 안전).
- 메운 조각은 이웃 벽 조각의 메시/머티리얼/회전/스케일을 그대로 복사해(ref) 같은 깊이(X)에 이어 붙인다. 라벨 `FILL_*`, 폴더 `Art/<Section>/WallFill`.
- 장식 메시는 NoCollision(프로젝트 규칙). 레인 경계에 걸리는 구멍이면 `COL_Fill_*` 박스(BlockAll, Camera 무시, 게임 중 숨김)를 함께 둔다.
- 구멍 찾기: PIE 실제 카메라(위아래 분할, 피치 -10, FOV 기준 화면 폭 약 19m)로 레인을 따라 캡처 → 하늘/빈 공간 색 픽셀을 벽 평면(X)에 역투영해서 찾았다.
- 결과 기록: `Docs/LevelManifests/WallFill_<Map>.json`

사용: 대상 맵을 에디터에서 연 상태로 이 파일을 에디터 Python으로 실행(열린 맵 이름으로 목록을 고른다). 저장은 직접(또는 SAVE=True).
"""
import unreal, json

TAG = "GEN_WallFill"
SAVE = False

# center = 목표 바운드 중심(월드). 메시 피벗은 바운드 중심(Soul City 피벗 중심화)이지만, 스폰 후 실제 바운드로 한 번 더 맞춘다.
# ref = 메시/머티리얼을 복사할 이웃 액터 라벨. scale/yaw 를 생략하면 ref 값을 쓴다.
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
    ],
    "Map2_SoulCity2": [
        # 시작 끝벽(Y -830, X -120~700) 너머 빈 하늘 → 끝벽과 같은 산업 벽판(같은 Z 스케일 1.43)을 끝벽 뒤에 ㄱ자로 붙인다
        dict(label="FILL_S1_StartBackdrop", ref="SM_Slums_Wall_03a", folder="Art/S1_FloodedCanal/WallFill",
             center=(740, -1380, 149.75), yaw=90, scale=(1.0, 0.94, 1.43)),
        # 출구 게이트 벽(Y 14232) 너머 빈 하늘 → 같은 방식
        dict(label="FILL_S5_EndBackdrop", ref="SM_Slums_Wall_03a2", folder="Art/S5_CaveDescent/WallFill",
             center=(740, 14940, 83.0), yaw=90, scale=(1.1667, 0.94, 1.6)),
    ],
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
        a = _place_col(f) if "box" in f else _place_art(f)
        l = a.get_actor_location(); r = a.get_actor_rotation(); s = a.get_actor_scale3d()
        o, e = a.get_actor_bounds(False)
        out.append({
            "label": a.get_actor_label(), "folder": str(a.get_folder_path()),
            "kind": "collision" if "box" in f else "art",
            "mesh": a.static_mesh_component.static_mesh.get_path_name(), "ref": f.get("ref"),
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
