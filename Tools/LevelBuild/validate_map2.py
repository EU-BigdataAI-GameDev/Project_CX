"""Map2 검증: 재로드 후 manifest 일치, 콜리전 설정, 라이트 없음, 카메라 앞 가림 규칙, 캡슐 서기/통과 테스트."""
import unreal, json
LEVEL, TAG = "/Game/Project_CX/Maps/Map2", "GEN_Map2"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level("/Game/Project_CX/Maps/Map1")
les.load_level(LEVEL)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = eas.get_all_level_actors()
gen = [a for a in actors if TAG in [str(t) for t in a.tags]]
m = json.load(open(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map2_manifest.json"), encoding="utf-8"))
print("generated", len(gen), "manifest", len(m["actors"]), "match", len(gen) == len(m["actors"]))
gm = world.get_world_settings().get_editor_property("default_game_mode")
print("GameMode", gm.get_name() if gm else None, "| KillZ", world.get_world_settings().get_editor_property("kill_z"))
print("PlayerStarts", [(a.get_actor_label(), str(a.get_editor_property("player_start_tag"))) for a in actors if isinstance(a, unreal.PlayerStart)])
lights = [a.get_actor_label() for a in actors if isinstance(a, (unreal.Light, unreal.SkyLight, unreal.ExponentialHeightFog, unreal.PostProcessVolume, unreal.SkyAtmosphere, unreal.VolumetricCloud))]
print("light/atmosphere actors:", lights)

cols, bad = [], []
for a in gen:
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    c = a.static_mesh_component
    if a.get_actor_label().startswith("COL_"):
        cols.append(a)
        if c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) != unreal.CollisionResponseType.ECR_BLOCK or \
           c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA) != unreal.CollisionResponseType.ECR_IGNORE or \
           c.is_visible() or not c.get_editor_property("hidden_in_game"):
            bad.append(a.get_actor_label())
    elif str(c.get_collision_profile_name()) != "NoCollision" or c.static_mesh is None:
        bad.append(a.get_actor_label())
print("COL boxes", len(cols), "| bad collision/visibility:", bad[:8])

# 카메라 앞(X < -120) 규칙: 발판 높이를 넘는 메시가 없어야 한다
walk = [e for e in m["actors"] if e["kind"] == "collision"]
def surface_top(y0, y1):
    tops = [e["box"][5] for e in walk if e["box"][2] < y1 and e["box"][3] > y0 and e["box"][0] <= -100]
    return max(tops) if tops else None
viol = []
for a in gen:
    if a.get_actor_label().startswith("COL_") or not isinstance(a, unreal.StaticMeshActor):
        continue
    o, ext = a.get_actor_bounds(False)
    if o.x - ext.x < -125:
        top = surface_top(o.y - ext.y, o.y + ext.y)
        if top is None or o.z + ext.z > top + 5:
            viol.append((a.get_actor_label(), round(o.x - ext.x), round(o.z + ext.z), top))
print("front-occluder violations:", len(viol), viol[:10])

# 캡슐 테스트: 모든 발판 윗면에서 여동생/오빠가 설 수 있는지
col_set = set(a.get_actor_label() for a in cols)
def blocked(x, y, z_feet, hh):
    hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(x, y, z_feet + hh + 3), 34.0, hh,
                                                          [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], unreal.StaticMeshActor, [])
    return [h.get_actor_label() for h in (hits or []) if h.get_actor_label() in col_set]
report = {"sister_blocked": [], "brother_blocked": []}
for e in walk:
    x0, x1, y0, y1, z0, z1 = e["box"]
    if e["label"] in ("COL_S3_Roof",) or "Ceiling" in e["label"]:
        continue
    xs = [max(x0, -120) + 40, min(x1, 150) - 40]
    y = y0 + 40
    while y < y1 - 39:
        for x in xs:
            if x < -120 + 34 or x > 150 - 34:
                continue
            for who, hh in (("sister", 80.0), ("brother", 88.0)):
                h = blocked(x, y, z1, hh)
                if h:
                    report[who + "_blocked"].append((e["label"], round(x), round(y), h[0]))
        y += 60
def summarize(lst):
    out = {}
    for lab, x, y, hit in lst:
        k = (lab, hit)
        out.setdefault(k, [y, y])
        out[k] = [min(out[k][0], y), max(out[k][1], y)]
    return out
for who in ("sister", "brother"):
    s = summarize(report[who + "_blocked"])
    print(who, "cannot stand at:", {f"{k[0]}<-{k[1]}": v for k, v in s.items()})
