"""
레인 안 지형지물(소품/장치/기둥/난간 등)에 투명 충돌 박스(COL_Prop_*)를 붙인다. 레벨 수정용, 재실행 안전(기존 COL_Prop_* 를 지우고 다시 만듦).

대상: 레인과 겹치고, 높이 15cm 이상, 발판에서 200cm 이내(머리 위에 매달린 것 제외),
      이미 지형 충돌 박스에 절반 이상 묻혀 있지 않은 메시(= 바닥 타일/계단/벽판 등은 이미 충돌이 있음)
제외: 매달린 장식(체인, 빨래, 전선, 간판, 창문, 전등, 천장 배관 — 점프/계단에서 머리를 막지 않도록), 여동생 통로 처마/배관(통로를 좁히지 않도록), 바닥 타일, 끝벽
박스: 메시 바운드 그대로. 단, 발판에서 90cm 미만 떠 있는 것(난간 등)은 발판까지 내려서 밑으로 빠져나가지 못하게 한다.
"""
import unreal, json
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
SKIP_FOLDERS = {"Ground", "Structure", "Wires", "Chains", "CanalWalls", "SisterDuct", "RoofTop", "Laundry", "Water", "Ceiling", "Lamps", "Lights", "Channel"}
SKIP_MESH = ("Cloth", "Sign", "Window", "Wires", "Chain", "Plane", "Water", "Decal", "Light_", "Cieling")
MAPS = [("/Game/Project_CX/Maps/Map1_SoulCity", "GEN_Map1SoulCity", -250, 250, "Map1_SoulCity_manifest.json"),
        ("/Game/Project_CX/Maps/Map2", "GEN_Map2", -120, 150, "Map2_manifest.json")]
mdir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/")

for LEVEL, TAG, LF, LB, MAN in MAPS:
    les.load_level(LEVEL)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    removed = set()
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith("COL_Prop_"):
            removed.add(a.get_actor_label()); eas.destroy_actor(a)
    acts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)]
    cols = []
    for a in acts:
        if a.get_actor_label().startswith("COL_"):
            o, e = a.get_actor_bounds(False)
            cols.append((o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z))
    def inside_frac(b):
        vol = max(1e-3, (b[1]-b[0])*(b[3]-b[2])*(b[5]-b[4])); best = 0
        for c in cols:
            ix = max(0, min(b[1], c[1]) - max(b[0], c[0])); iy = max(0, min(b[3], c[3]) - max(b[2], c[2])); iz = max(0, min(b[5], c[5]) - max(b[4], c[4]))
            best = max(best, ix * iy * iz / vol)
        return best
    new = []
    for a in acts:
        lab = a.get_actor_label()
        f = str(a.get_folder_path())
        if lab.startswith("COL_") or not f.startswith("Art") or f.split("/")[-1] in SKIP_FOLDERS:
            continue
        mname = a.static_mesh_component.static_mesh.get_name()
        if any(k in mname for k in SKIP_MESH):
            continue
        o, e = a.get_actor_bounds(False)
        b = [o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z]
        if b[1] < LF + 5 or b[0] > LB - 5 or b[5] - b[4] < 15:
            continue
        h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(o.x, o.y, b[4] + 5), unreal.Vector(o.x, o.y, b[4] - 400),
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [a], unreal.DrawDebugTrace.NONE, True)
        floor = h.to_tuple()[4].z if h else b[4]
        if b[4] > floor + 200 or inside_frac(b) > 0.5:
            continue
        if 0 < b[4] - floor < 90:
            b[4] = floor
        # 레인 밖으로 나간 부분은 잘라낸다(경계벽과 겹칠 필요 없음)
        b[0], b[1] = max(b[0], LF), min(b[1], LB)
        c = eas.spawn_actor_from_object(CUBE, unreal.Vector((b[0]+b[1])/2, (b[2]+b[3])/2, (b[4]+b[5])/2))
        c.set_actor_scale3d(unreal.Vector((b[1]-b[0])/100, (b[3]-b[2])/100, (b[5]-b[4])/100))
        comp = c.static_mesh_component
        comp.set_collision_profile_name("BlockAll")
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
        comp.set_editor_property("visible", False)
        comp.set_editor_property("hidden_in_game", True)
        comp.set_editor_property("cast_shadow", False)
        c.set_folder_path(f.replace("Art/", "Collision/", 1))
        c.set_actor_label("COL_Prop_" + lab)
        c.tags = [TAG]
        new.append((c, b, lab))
    les.save_current_level()
    m = json.load(open(mdir + MAN, encoding="utf-8"))
    m["actors"] = [x for x in m["actors"] if x["label"] not in removed]
    for c, b, lab in new:
        l = c.get_actor_location(); s = c.get_actor_scale3d()
        m["actors"].append({"label": c.get_actor_label(), "kind": "blocker", "folder": str(c.get_folder_path()), "for": lab,
                            "loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)], "rot": [0, 0, 0],
                            "scale": [round(s.x, 3), round(s.y, 3), round(s.z, 3)], "box": [round(v, 1) for v in b]})
    json.dump(m, open(mdir + MAN, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(LEVEL.split("/")[-1], "prop collision boxes:", len(new))
