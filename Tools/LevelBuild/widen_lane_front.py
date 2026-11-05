"""
레인 앞쪽(카메라 쪽, -X) 깊이 넓히기 — Map1_SoulCity1 / Map2_SoulCity2 (2026-11-04, 사용자 요청).

요청: "깊이가 조금 늘었는데 앞으로 나오려면 투명벽이 막는다. 투명벽을 화면 쪽으로 당겨 달라" + "종(W/S) 길이 1.1배".
  → 걸을 수 있는 깊이(앞 경계 안쪽 면 ~ 뒤 경계 안쪽 면)를 DEPTH_FACTOR 배로, **앞쪽(카메라 쪽)으로만** 늘린다. Z 는 건드리지 않는다.
     Map1: 레인 X -260 ~ 260 (520) → 1.1배 572 → 앞 경계 안쪽 면 -260 → -312 (DELTA 52)
     Map2: 레인 X -120 ~ 150 (270) → 1.1배 297 → 앞 경계 안쪽 면 -120 → -147 (DELTA 27)
     2026-11-06: Map2 는 1.1배(+27cm)가 체감되지 않아 1.5배 405 → 앞 경계 안쪽 면 -255 (DELTA 135). undo(원래 -120 으로) 후 1.5배로 다시 적용.
       맵마다 배율이 다르므로 MAPS[...]["factor"] 에 둔다(Map1 1.1, Map2 1.5). LN_DEPTH_FACTOR 를 주면 그 값이 우선.
  카메라 실측(PIE): 스프링암(1601, 피치 -10, FOV 37.5)이 캐릭터 X 를 그대로 따라간다(지연 없음) → 캐릭터가 어느 깊이에 서도
  화면 크기·발 위치가 같다(발 = 화면 높이의 89%). 그래서 깊이 배율은 화면 구도로 제한되지 않는다(1.3 도 구도는 같음).

규칙(축 정렬, yaw 0/90/180/270 만 — 그 밖의 회전은 건드리지 않고 목록으로 보고):
  A) 앞 끝을 늘림: 정적 메시(아트·COL·네온)의 앞면 b[0] 이 옛 앞 경계 근처([OLD-45, OLD+12])이고 X 폭 > 30 →
     뒤 끝은 그대로 두고 앞 끝만 DELTA 만큼 앞으로(X 스케일 늘림). 바닥 COL·발판·계단·경사(yaw 90 + 피치 → 로컬 Y)·게이트 COL·
     게이트 네온 레일·바닥 타일·발판 아트·계단 아트·끝벽·S3 칸막이 벽·여동생 덕트 천장(COL + 처마 아트)이 여기에 해당.
  B) 통째로 앞으로: 같은 범위의 얇은 것(X 폭 ≤ 30) — COL_Bound_Front, 블록 앞면 파사드(바닥 아래), 발판 앞 모서리 네온 띠.
  C) 양 끝 경계(COL_Bound_Start/End/Left/Right): 앞 끝이 새 앞 경계 - 40 보다 뒤면 앞 끝을 DELTA 만큼 늘림.
  D) 레벨 출구 ZoneExtent.X += DELTA(액터는 그대로 — 앞 끝이 새 경계까지 덮음).
  E) 해킹 게이트 네온 막대: 새로 생긴 앞쪽 폭에 같은 간격으로 막대 추가(게이트 태그 + 이 스크립트 태그).
  제외: 엄폐물(라벨/폴더에 Cover), COL_Prop_*(소품 — 레인 앞 소품 COL 은 옮기지 않음), COL_Bound_Back, EXCLUDE_LABELS(출구 문 위 네온).
  가림 규칙: 바닥 높이를 넘는 새 메시는 만들지 않는다(앞으로 늘어난 키 큰 메시는 전부 원래 앞 경계에 있던 칸막이/끝벽 — 레인 안쪽).

안전
  - 표식 액터(태그 GEN_LaneFront, 태그 'LaneFrontDelta=<cm>' — add_map*_hacking.py 가 읽음) 또는 백업 manifest 가 있으면 거부.
  - 실행 전 dirty 면 거부. 바꾼 액터의 원래 변환/출구 구역은 Docs/LevelManifests/<Map>_lane_front_backup.json → undo 로 복원.
  - 검사(기존 scale_map*_vertical.py 경로 검사 + 앞쪽 검사) 실패면 저장하지 않는다.
실행(에디터 원격 실행): 1회차 — 현재 레벨이 대상이 아니면 불러오고 끝. 2회차 — 적용 + 검사 + 저장.
  globals(): LN_LEVEL = "/Game/Project_CX/Maps/Map2_SoulCity2" (기본: 현재 레벨), LN_MODE = "apply" | "dry" | "validate" | "undo",
             LN_DEPTH_FACTOR (기본 DEPTH_FACTOR), LN_NO_SAVE, LN_ALLOW_DIRTY(같은 세션에서 undo 한 직후 다시 적용할 때만).
"""
import unreal, json, os, math

DEPTH_FACTOR = 1.1          # 기본 깊이 배율(앞쪽으로만) — 맵별 값은 MAPS[...]["factor"]. 1.5 도 구도상 문제 없음(카메라가 X 를 따라감) — 아트 늘림이 커질 뿐.
TAG = "GEN_LaneFront"
CUBE = "/Engine/BasicShapes/Cube.Cube"
WIN_BEHIND, WIN_AHEAD = 12.0, 45.0     # 앞 끝 판정 범위: OLD - 45 ~ OLD + 12 (Map1 바닥 COL 은 경계 안쪽 10cm 에서 시작)
THIN = 30.0

MAPS = {
    "/Game/Project_CX/Maps/Map1_SoulCity1": dict(old_front=-260.0, back=260.0, factor=1.1, short="Map1_SoulCity1", lib="scale_map1_vertical.py",
                                                 hack_tag="GEN_Map1Hack", bar_step=50.0),
    "/Game/Project_CX/Maps/Map2_SoulCity2": dict(old_front=-120.0, back=150.0, factor=1.5, short="Map2_SoulCity2", lib="scale_map2_vertical.py",
                                                 hack_tag="GEN_Map2Hack", bar_step=40.0,
                                                 # 수로 판자 발판 P1~P4(COL + 판자 아트): 원래 앞 경계에서 20cm 안쪽(X -100)에 떠 있다 → 같이 앞으로 늘림(체인은 뒤쪽 X 77~133)
                                                 extra_front=dict(labels=("COL_S1_Pallet_",), folders=("/Pallets",))),
}
BRO, SIS = (34.0, 88.0), (34.0, 80.0)
# 출구 문 위 네온(두 기둥 사이 — 기둥은 그대로이므로 늘리지 않음)
EXCLUDE_LABELS = {"NEON_S5_Gate_Top"}


def _dir():
    return os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Tools/LevelBuild")


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _bounds(a):
    o, e = a.get_actor_bounds(False)
    return [o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z]


def _v(l):
    return [l.x, l.y, l.z]


def _tags(a):
    return [str(t) for t in a.tags]


def _manifest_path(cfg):
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/%s_lane_front_backup.json" % cfg["short"])


def _factor(cfg):
    return globals().get("LN_DEPTH_FACTOR", cfg.get("factor", DEPTH_FACTOR))


def _delta(cfg):
    f = _factor(cfg)
    return round((cfg["back"] - cfg["old_front"]) * (f - 1.0), 1)


def _x_axis(a):
    """월드 X 를 따르는 로컬 축 인덱스(0=X, 1=Y). 축 정렬이 아니면 None."""
    r = a.get_actor_rotation()
    yaw = round(r.yaw) % 180
    if abs(r.yaw - round(r.yaw)) > 0.05 or yaw not in (0, 90) or abs(r.roll) > 0.05:
        return None
    if yaw == 0 and abs(r.pitch) > 0.05:
        return None            # 피치가 로컬 X 를 기울임
    return 0 if yaw == 0 else 1


def _stretch_front(a, d):
    """뒤 끝(b[1])은 두고 앞 끝만 d 만큼 -X 로."""
    ax = _x_axis(a)
    b = _bounds(a)
    w = b[1] - b[0]
    s = a.get_actor_scale3d()
    k = (w + d) / w
    sc = [s.x, s.y, s.z]
    sc[ax] *= k
    a.set_actor_scale3d(unreal.Vector(*sc))
    b2 = _bounds(a)
    l = a.get_actor_location()
    a.set_actor_location(unreal.Vector(l.x + (b[1] - b2[1]), l.y, l.z), False, False)


def _translate(a, d):
    l = a.get_actor_location()
    a.set_actor_location(unreal.Vector(l.x - d, l.y, l.z), False, False)


def plan(cfg, d):
    old = cfg["old_front"]
    new = old - d
    out = {"stretch": [], "move": [], "bound_ends": [], "skipped_rot": [], "excluded": []}
    for a in _eas().get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor) or a.get_attach_parent_actor():
            continue
        lab = a.get_actor_label()
        f = str(a.get_folder_path())
        b = _bounds(a)
        if lab.startswith("COL_Bound") and lab not in ("COL_Bound_Front", "COL_Bound_Back"):
            if b[0] > new - 40:
                out["bound_ends"].append(a)
            continue
        ex = cfg.get("extra_front", {})
        extra = lab.startswith(tuple(ex.get("labels", ()))) or f.endswith(tuple(ex.get("folders", ()))) if ex else False
        if lab == "COL_Bound_Back" or not (old - WIN_AHEAD <= b[0] <= old + WIN_BEHIND or extra):
            continue
        if "Cover" in lab or "/Cover" in f or lab.startswith("COL_Prop_") or lab in EXCLUDE_LABELS:
            out["excluded"].append(a)
            continue
        if _x_axis(a) is None:
            out["skipped_rot"].append(a)
            continue
        (out["move"] if b[1] - b[0] <= THIN else out["stretch"]).append(a)
    return out


def _gate_bar_additions(cfg, d):
    """게이트마다 앞쪽 새 폭에 네온 막대 추가 위치."""
    new = cfg["old_front"] - d
    adds = []
    gates = {}
    for a in _eas().get_all_level_actors():
        lab = a.get_actor_label()
        if lab.startswith("NEON_HackGate_") and "_Rail_" not in lab:
            g = lab.rsplit("_", 1)[0]
            gates.setdefault(g, []).append(a)
    for g, bars in gates.items():
        bars.sort(key=lambda a: a.get_actor_location().x)
        first = bars[0]
        x = first.get_actor_location().x - cfg["bar_step"]
        i = 0
        while x >= new + 5.0:
            adds.append((first, "%s_F%d" % (g, i), x))
            x -= cfg["bar_step"]
            i += 1
    return adds


def _spawn_marker(cfg, d):
    cls = unreal.Note if hasattr(unreal, "Note") else unreal.TargetPoint
    m = _eas().spawn_actor_from_class(cls, unreal.Vector(0, -600, 400))
    m.set_actor_label("INFO_LaneFront_x%.2f" % _factor(cfg))
    m.set_folder_path("Info")
    m.tags = [TAG, "LaneFrontDelta=%.1f" % d]
    try:
        m.set_editor_property("text", "lane front widened by %.1f cm toward camera (Tools/LevelBuild/widen_lane_front.py). Backup: Docs/LevelManifests/%s_lane_front_backup.json"
                              % (d, cfg["short"]))
    except Exception:
        pass
    return m


def apply(cfg, dry=False):
    d = _delta(cfg)
    actors = _eas().get_all_level_actors()
    if any(TAG in _tags(a) for a in actors) or os.path.exists(_manifest_path(cfg)):
        print("REFUSED: already applied (actor tagged %s or %s exists). Use LN_MODE='undo' first." % (TAG, _manifest_path(cfg)))
        return False
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if dirty and not dry and not globals().get("LN_ALLOW_DIRTY"):     # LN_ALLOW_DIRTY: 이 스크립트의 undo 직후(자기 변경만 dirty)일 때만
        print("REFUSED: map packages already dirty (someone's unsaved edits):", dirty)
        return False
    p = plan(cfg, d)
    adds = _gate_bar_additions(cfg, d)
    print("lane front: old inner X %.0f -> new %.0f (DELTA %.1f, factor %.2f, depth %.0f -> %.0f)"
          % (cfg["old_front"], cfg["old_front"] - d, d, _factor(cfg), cfg["back"] - cfg["old_front"], cfg["back"] - cfg["old_front"] + d))
    for k in ("stretch", "move", "bound_ends", "skipped_rot", "excluded"):
        print("  %s %d:" % (k, len(p[k])), sorted(a.get_actor_label() for a in p[k]))
    print("  gate bars to add %d:" % len(adds), [(n, x) for _, n, x in adds])
    exits = [a for a in actors if a.get_class().get_name() == "CXLevelExit"]
    print("  level exits:", [(a.get_actor_label(), _v(a.get_editor_property("zone_extent"))) for a in exits])
    if dry:
        return False

    backup = {"level": cfg["short"], "delta": d, "factor": _factor(cfg), "actors": [], "created": [], "exit_zone": {}}
    for a in p["stretch"] + p["move"] + p["bound_ends"]:
        r = a.get_actor_rotation()
        backup["actors"].append({"name": a.get_name(), "label": a.get_actor_label(), "loc": _v(a.get_actor_location()),
                                 "rot": [r.pitch, r.yaw, r.roll], "scale": _v(a.get_actor_scale3d())})
    for a in p["stretch"] + p["bound_ends"]:
        _stretch_front(a, d)
    for a in p["move"]:
        _translate(a, d)
    for src, label, x in adds:
        l = src.get_actor_location()
        n = _eas().spawn_actor_from_object(src.static_mesh_component.static_mesh, unreal.Vector(x, l.y, l.z))
        n.set_actor_scale3d(src.get_actor_scale3d())
        c = n.static_mesh_component
        c.set_material(0, src.static_mesh_component.get_material(0))
        c.set_collision_profile_name("NoCollision")
        c.set_editor_property("cast_shadow", False)
        n.set_folder_path(str(src.get_folder_path()))
        n.set_actor_label(label)
        n.tags = _tags(src) + [TAG]
        backup["created"].append(n.get_name())
        # 해킹 단말기가 게이트 해제 때 이 막대도 숨기도록 unlock_actors 에 추가
        for t in unreal.GameplayStatics.get_all_actors_of_class(_world(), unreal.HackTerminal):
            ul = list(t.get_editor_property("unlock_actors"))
            if src in ul:
                t.set_editor_property("unlock_actors", ul + [n])
    for a in exits:
        ze = a.get_editor_property("zone_extent")
        backup["exit_zone"][a.get_name()] = _v(ze)
        a.set_editor_property("zone_extent", unreal.Vector(ze.x + d, ze.y, ze.z))
    m = _spawn_marker(cfg, d)
    backup["created"].append(m.get_name())
    json.dump(backup, open(_manifest_path(cfg), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    print("applied: stretched %d, moved %d, bound ends %d, bars +%d, exits %d -> backup %s"
          % (len(p["stretch"]), len(p["move"]), len(p["bound_ends"]), len(adds), len(exits), _manifest_path(cfg)))
    return True


def undo(cfg):
    path = _manifest_path(cfg)
    if not os.path.exists(path):
        print("no backup:", path)
        return
    bk = json.load(open(path, encoding="utf-8"))
    eas = _eas()
    acts = {a.get_name(): a for a in eas.get_all_level_actors()}
    created = [acts[n] for n in bk["created"] if n in acts]
    for t in unreal.GameplayStatics.get_all_actors_of_class(_world(), unreal.HackTerminal):
        ul = list(t.get_editor_property("unlock_actors"))
        keep = [x for x in ul if x not in created]
        if len(keep) != len(ul):
            t.set_editor_property("unlock_actors", keep)
    n_del = 0
    for a in created:
        eas.destroy_actor(a); n_del += 1
    for a in eas.get_all_level_actors():
        if TAG in _tags(a):
            eas.destroy_actor(a); n_del += 1
    acts = {a.get_name(): a for a in eas.get_all_level_actors()}
    n = 0
    for e in bk["actors"]:
        a = acts.get(e["name"])
        if not a:
            continue
        a.set_actor_rotation(unreal.Rotator(pitch=e["rot"][0], yaw=e["rot"][1], roll=e["rot"][2]), False)
        a.set_actor_scale3d(unreal.Vector(*e["scale"]))
        a.set_actor_location(unreal.Vector(*e["loc"]), False, False)
        n += 1
    for nm, ze in bk.get("exit_zone", {}).items():
        if nm in acts:
            acts[nm].set_editor_property("zone_extent", unreal.Vector(*ze))
    os.rename(path, path + ".undone")
    print("undo: restored", n, "actors, deleted", n_del, "created — save the level to keep")


# ------------------------------------------------------------------------------------------------ validate
def _lib(cfg):
    g = {"__name__": "ln_lib", "VZ_AUTORUN": False}
    exec(open(os.path.join(_dir(), cfg["lib"]), encoding="utf-8").read(), g)
    if "_lib" in g:                    # scale_map1_vertical.py 는 scale_map2_vertical.py 를 Map1 설정으로 감싼다
        return g, g["_lib"]()
    return g, g


def front_checks(cfg, d):
    world = _world()
    actors = _eas().get_all_level_actors()
    by = {a.get_actor_label(): a for a in actors}
    _, lib = _lib(cfg)
    ignore = lib["_col_ignore"](actors)
    free, down, walk = lib["make_tools"](world, ignore)
    old, back = cfg["old_front"], cfg["back"]
    new = old - d
    fails = []

    def check(ok, msg, quiet=False):
        if not ok or not quiet:
            print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            fails.append(msg)

    # 1) 경계: 앞 경계 안쪽 면 = new, 뒤 경계 그대로
    bf, bb = _bounds(by["COL_Bound_Front"]), _bounds(by["COL_Bound_Back"])
    check(abs(bf[1] - new) < 0.6 and abs(bb[0] - back) < 0.6, "bounds inner faces front %.1f (want %.1f) back %.1f -> walkable depth %.0f" % (bf[1], new, bb[0], bb[0] - bf[1]))
    ys, ye = bf[2], bf[3]

    # 아트 바닥(바운드) 목록 — 보이는 정적 메시 중 납작하거나 바닥 덩어리
    art = []
    for a in actors:
        if isinstance(a, unreal.StaticMeshActor) and not a.get_actor_label().startswith(("COL_", "NEON_")):
            c = a.static_mesh_component
            if c.get_editor_property("visible") and not a.is_hidden_ed():
                art.append(_bounds(a))

    def art_under(x, y, z):
        return any(b[0] <= x <= b[1] and b[2] <= y <= b[3] and z - 35 <= b[5] <= z + 6 for b in art)

    # 2) 옛 앞 가장자리(old+36)에서 설 수 있는 모든 바닥 → 새 앞 가장자리(new+36)에서도 같은 높이에 설 수 있고,
    #    경계 바로 앞(new+22 — Map1 바닥 COL 은 원래도 경계 안쪽 10cm, Map2 수로 판자는 20cm 에서 시작. 캡슐 중심은 new+34 까지만 감)에도 같은 바닥, 그 밑에 아트 바닥.
    n_samp, n_fail, n_art = 0, 0, 0
    xo, xn = old + 36.0, new + 36.0
    y = ys + 40.0
    while y < ye - 40.0:
        z_from = 4000.0
        for _ in range(8):
            z, act = down(xo, y, z_from, 6000.0)
            if z is None:
                break
            alab = act.get_actor_label() if act else ""
            for cap, nm in ((BRO, "brother"), (SIS, "sister")):
                if "Cover" in alab or alab.startswith("COL_Prop_") or not free(xo, y, z, cap)[0]:
                    continue        # 엄폐물·소품 윗면은 앞으로 늘리지 않는다(바닥이 아님)
                n_samp += 1
                zn, an = down(xn, y, z + 30.0)
                ze, ae = down(new + 22.0, y, z + 30.0)
                ok = zn is not None and abs(zn - z) < 1.0 and free(xn, y, z, cap)[0] and ze is not None and abs(ze - z) < 1.0
                if not ok:
                    n_fail += 1
                    if n_fail <= 25:
                        check(False, "front edge %s Y%.0f z%.1f (%s): new-edge floor %s (%s), at bound %s, capsule free %s %s" % (
                            nm, y, z, act.get_actor_label() if act else None, None if zn is None else round(zn, 1),
                            an.get_actor_label() if an else None, None if ze is None else round(ze, 1), free(xn, y, z, cap)[0], free(xn, y, z, cap)[1][:2]))
                elif art_under(old + 22.0, y, z) and not art_under(new + 22.0, y, z):   # 원래 아트가 있던 바닥만
                    n_art += 1
                    if n_art <= 15:
                        print("WARN no art floor under new front edge Y%.0f z%.1f (%s)" % (y, z, act.get_actor_label() if act else None))
            z_from = z - 25.0
        y += 25.0
    check(n_fail == 0, "front-edge standing samples: %d, failed %d, missing art %d" % (n_samp, n_fail, n_art))
    if n_art:
        fails.append("missing art under new front edge at %d samples" % n_art)

    # 3) 앞으로 걸어가면 앞 경계에서 멈춤(경계 무시 목록에서 앞 경계만 빼서) — 바닥이 있는 곳 Y 500 간격
    ign2 = [i for i in ignore if i.get_actor_label() != "COL_Bound_Front"]
    n_stop = 0
    y = ys + 100.0
    while y < ye - 50.0:
        z, _ = down(0.0, y, 4000.0, 6000.0)
        if z is not None:
            h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(0, y, z + 60), unreal.Vector(new - 200, y, z + 60),
                                                       unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ign2, unreal.DrawDebugTrace.NONE, True)
            t = h.to_tuple() if h else None
            if t and t[9].get_actor_label() == "COL_Bound_Front":
                n_stop += 1
        y += 500.0
    check(n_stop > 0, "walking toward camera stops at COL_Bound_Front (%d sample rows reach the bound unobstructed)" % n_stop)

    # 4) 해킹 게이트: 새 앞 가장자리 줄(new+36)과 경계 바로 안쪽(new+12)에서도 막힘, 앞 끝이 새 경계(+12) 안
    for a in actors:
        lab = a.get_actor_label()
        if not lab.startswith("COL_HackGate"):
            continue
        b = _bounds(a)
        fz = max((down(0.0, yy, b[5])[0] or -1e9) for yy in (b[2] - 60, b[3] + 60))
        ig = [i for i in ignore if i != a]
        res = []
        for x in (new + 12.0, new + 36.0):
            h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, b[2] - 120, fz + 90), unreal.Vector(x, b[3] + 120, fz + 90),
                                                       unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ig, unreal.DrawDebugTrace.NONE, True)
            res.append(h.to_tuple()[9].get_actor_label() if h else None)
        check(all(r == lab for r in res) and b[0] <= new + 12.0, "gate %s X %.0f..%.0f blocks at new front rows %s" % (lab, b[0], b[1], res))

    # 5) 여동생 덕트(Map2): 새 앞 가장자리 줄에서도 여동생 통과 / 오빠 불가
    if "COL_S2_DuctCeiling" in by:
        for yy in (3200.0, 4300.0, 5400.0):
            for x in (new + 35.0, -70.0):
                check(free(x, yy, 0.0, SIS)[0] and not free(x, yy, 0.0, BRO)[0], "sister duct X%.0f Y%.0f: sister fits, brother blocked" % (x, yy))
        check(_bounds(by["COL_S2_DuctCeiling"])[0] <= new - 15, "duct ceiling reaches new front (X %.0f)" % _bounds(by["COL_S2_DuctCeiling"])[0])

    # 6) 레벨 출구 구역이 새 앞 가장자리 캡슐 중심(new+34)을 덮음
    for a in actors:
        if a.get_class().get_name() == "CXLevelExit":
            ze = a.get_editor_property("zone_extent"); l = a.get_actor_location()
            check(l.x - ze.x <= new + 34.0 and l.x + ze.x >= back - 34.0, "exit %s zone X %.0f..%.0f covers %.0f..%.0f" % (
                a.get_actor_label(), l.x - ze.x, l.x + ze.x, new + 34, back - 34))

    # 7) 카메라 앞 가림: 새 앞 경계보다 앞(X < new)에서 바닥 높이를 넘는 메시가 새로 생기지 않았는지 — 이 스크립트가 늘린 것만 검사
    for a in actors:
        if not isinstance(a, unreal.StaticMeshActor) or a.get_actor_label().startswith("COL_"):
            continue
        b = _bounds(a)
        if b[0] < new - 25.0 and b[0] > new - 60.0 and b[5] - b[4] > 120:
            print("NOTE tall art front at X %.0f: %s (Z %.0f..%.0f)" % (b[0], a.get_actor_label(), b[4], b[5]))
    print("FRONT CHECKS:", "ALL PASS" if not fails else "%d FAIL" % len(fails))
    return fails


def validate(cfg, d):
    g, lib = _lib(cfg)
    fails = []
    if "validate1" in g:
        fails += g["validate1"](lib)
    else:
        fails += lib["validate"]()
    fails += front_checks(cfg, d)
    return fails


def _current():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    return les.get_current_level().get_outermost().get_name() if les.get_current_level() else ""


def run():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    level = globals().get("LN_LEVEL") or _current()
    if level not in MAPS:
        print("not a target level:", level, "— set LN_LEVEL to one of", list(MAPS))
        return
    if _current() != level:
        unreal.SystemLibrary.collect_garbage()
        print("load_level:", les.load_level(level), "— run this script again")
        return
    cfg = MAPS[level]
    mode = globals().get("LN_MODE", "apply")
    if mode == "undo":
        undo(cfg)
        return
    if mode == "validate":
        marker = [t for a in _eas().get_all_level_actors() for t in _tags(a) if t.startswith("LaneFrontDelta=")]
        d = float(marker[0].split("=")[1]) if marker else 0.0
        validate(cfg, d)
        return
    if not apply(cfg, dry=(mode == "dry")):
        return
    fails = validate(cfg, _delta(cfg))
    if fails:
        print("NOT SAVED — validation failed (LN_MODE='undo' restores, or fix and save manually)")
    elif globals().get("LN_NO_SAVE"):
        print("NOT SAVED — LN_NO_SAVE")
    else:
        print("saved:", les.save_current_level())


if globals().get("LN_AUTORUN", True):
    run()
