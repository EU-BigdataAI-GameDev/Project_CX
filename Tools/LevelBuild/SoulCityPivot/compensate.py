# 피벗을 옮긴 메시를 쓰는 액터/인스턴스의 위치를 보정해 월드상 모습을 그대로 유지. MAP 을 앞에 붙여 실행
import unreal, json
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
plan = {e["path"].split("/")[-1]: unreal.Vector(*e["offset"]) for e in json.load(open(unreal.Paths.project_saved_dir() + "soulcity_pivot_plan.json"))["plan"]}
les.load_level(MAP)
done = {"comp": 0, "inst": 0, "child_restore": 0}
def off_for(mesh):
    if mesh is None: return None
    if not mesh.get_path_name().startswith("/Game/SoulCity/"): return None
    return plan.get(mesh.get_name())
for a in eas.get_all_level_actors():
    for comp in a.get_components_by_class(unreal.StaticMeshComponent):
        c = off_for(comp.static_mesh)
        if c is None: continue
        if isinstance(comp, unreal.InstancedStaticMeshComponent):
            for i in range(comp.get_instance_count()):
                t = comp.get_instance_transform(i, False)
                d = t.rotation.rotate_vector(unreal.Vector(c.x * t.scale3d.x, c.y * t.scale3d.y, c.z * t.scale3d.z))
                t.translation = t.translation + d
                comp.update_instance_transform(i, t, False, False, True)
                done["inst"] += 1
            continue
        t = comp.get_world_transform()
        d = t.rotation.rotate_vector(unreal.Vector(c.x * t.scale3d.x, c.y * t.scale3d.y, c.z * t.scale3d.z))
        kids = [(k, k.get_world_location()) for k in comp.get_children_components(False)]
        a.modify(); comp.modify()
        comp.set_world_location(comp.get_world_location() + d, False, False)
        for k, loc in kids:
            k.set_world_location(loc, False, False); done["child_restore"] += 1
        done["comp"] += 1
print(MAP, done)
if MAP.endswith("Map1_SoulCity"):
    mp = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map1_SoulCity_manifest.json")
    m = json.load(open(mp, encoding="utf-8"))
    byl = {x.get_actor_label(): x for x in eas.get_all_level_actors()}
    n = 0
    for e in m["actors"]:
        x = byl.get(e["label"])
        if x is not None:
            l = x.get_actor_location(); e["loc"] = [round(l.x, 2), round(l.y, 2), round(l.z, 2)]; n += 1
    m["pivot_note"] = "SoulCity 메시 피벗 중심화(2026-09-28) 이후 위치로 갱신됨"
    json.dump(m, open(mp, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("manifest updated", n)
les.save_all_dirty_levels()
