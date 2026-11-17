"""
Map2_SoulCity2 세로(Z) 1.1배 + 수로 출구 수정 (2026-11-04, 사용자 요청 — 기존 액터 변환을 고치는 전체 레벨 편집).

요청
  1) 수로에 빠지면 못 나온다 — 수로 끝 계단을 오르면 위 발판이 머리를 막는다.
     원인: 판자 발판 P4(COL_S1_Pallet_P4, Y 1980~2240, 밑면 Z 20)가 출구 돌계단(Y 2240~2400, 42cm × 4) 시작 바로 위에 걸쳐 있다.
           2번째 계단(윗면 -126)에 오르는 순간 캡슐 중심이 Y≈2246 → 캡슐 윗반구(머리 끝 -126+176=50)가 P4 밑면(20)에 걸린다.
           여동생(키 160, 머리 34)도 같다. 계단에서 점프해도 머리 위 여유가 12cm뿐이라 못 나온다. 수로의 다른 출구는 없다(시작 쪽 옹벽 210).
  2) 맵 전체 세로 길이만 1.1배(가로 Y/깊이 X는 그대로).

방법
  - 기준 바닥 BASE_Z = 0 (S0/S2/S5 바닥 COL 윗면, 시작 지점 바닥). 모든 액터: Z' = BASE_Z + (Z - BASE_Z) * 1.1.
  - 정적 메시(아트, COL_ 박스, 벽 메우기, 네온 막대 등): Z 스케일도 × 1.1. 회전이 yaw만이면 로컬 Z = 월드 Z 라서 정확하다(피벗 위치와 무관).
    COL 경사(피치 ±30 큐브 5개): 윗면 양 끝점을 1.1배 한 뒤 길이·피치를 다시 계산(두께 유지) → 30° → 32.4°.
    피치/롤이 있는 아트 메시: 로컬 Z 스케일 × 1.1 근사(목록 출력).
  - 점 액터(PlayerStart, 거미, 해킹 단말기, 단서, 레벨 출구, 마커 TargetPoint): 늘리지 않고, 밑 COL 바닥에서의 높이를 유지(바닥만 1.1배 따라감).
    밑에 바닥이 없으면 Z만 1.1배. 레벨 출구는 구역 높이(ZoneExtent.Z)도 × 1.1.
  - 조명/포그/스카이/PPV: 위치 Z만 1.1배(PPV는 unbound — 스케일 Z도 1.1배). KillZ -1500 → -1650.
  - 예외(일부러 늘리지 않음): 여동생 전용 처마 통로 — 천장 170 은 "여동생 160 통과 / 오빠 176 불가" 분기의 핵심이라 1.1배(187) 하면 오빠가 통과한다.
      COL_S2_DuctCeiling, Art/S2_Split/SisterDuct(처마 양철판·배관), NEON_S2_DuctGuide, PL_S2_DuctFluo_* 는 그대로.
      COL_S2_AwningBlock 은 밑면 190(천장 윗면)을 유지하고 윗면만 1.1배.
  - 걸어서 나오던 구덩이 4곳(S2 PitA/PitB, S4 낮은 마당, S5 바위 구덩이)은 45 → 49.5 가 되어 자동 계단 오르기(MaxStepHeight 45)로 못 올라온다
    → 구덩이 바닥 COL 윗면을 5.5cm 올려 다시 44cm 로 맞추고, 구덩이 위에 얹힌 아트/엄폐물은 같이 올리고 바닥부터 선 벽판은 위 끝만 늘린다.
  - 수로 출구(1.1배 뒤 좌표): 바닥 -231, P4 밑면 22. 오빠 캡슐(반경 34)이 P4 끝에서 34cm 넘게 떨어져야 머리 높이(-126 이상) 계단에 오를 수 있으므로
      P4 를 Y 1980~2200 으로 40cm 줄이고(아트 판자·체인 같이), 돌계단을 5단(≤40cm)으로 다시 놓는다:
      -194(Y 2140~2185) → -160(~2275, P4 밑, 머리 16 < 22) → -120(2275~2317) → -80(~2358) → -40(~2400) → 바닥 0.
      P4 → S2 바닥 간격은 160 → 200(내려가는 점프, 높이차 -44 — 달리기 점프 도달 거리 약 4m).

안전
  - 한 번만 실행된다: 표식 액터(태그 GEN_Map2VScale) 또는 백업 manifest 가 있으면 거부.
  - 실행 전 Map2 가 dirty(저장 안 한 다른 수정)면 거부.
  - 원래 변환은 Docs/LevelManifests/Map2_vertical_scale_1.1_backup.json 에 기록 → undo() 로 되돌린다(만든 액터 삭제 + 변환 복원 + KillZ 복원).
  - 검사(validate) 실패면 저장하지 않는다.

실행(에디터 원격 실행): 1회차 — 현재 레벨이 Map2 가 아니면 불러오고 끝(로드는 자기 호출에서만). 2회차 — 적용 + 검사 + 저장.
되돌리기: 이 파일을 exec 한 뒤 MODE = "undo" 로 실행(globals()["VZ_MODE"] = "undo"). 검사만: VZ_MODE = "validate".
"""
import unreal, json, math, os

LEVEL = "/Game/Project_CX/Maps/Map2_SoulCity2"
TAG = "GEN_Map2VScale"
S = 1.1
BASE_Z = 0.0
KILLZ_NEW = -1650.0
MANIFEST_NAME = "Map2_vertical_scale_1.1_backup.json"
MARKER_LABEL = "INFO_Map2_VerticalScale_x1.1"
LANE_CLAMP = (-90.0, 100.0)          # 점 액터 바닥을 잴 때 X 를 레인 안쪽으로
CUBE = "/Engine/BasicShapes/Cube.Cube"
ROCK_SLAB = "SM_Cave_Rock_Flat01"

KEEP_LABELS = {"COL_S2_DuctCeiling", "NEON_S2_DuctGuide"}
KEEP_PREFIX = ("PL_S2_DuctFluo_",)
KEEP_FOLDERS = ("Art/S2_Split/SisterDuct",)
FIXED_BOTTOM = {"COL_S2_AwningBlock": 190.0}
POINT_CLASSES = {"PlayerStart", "SpiderBot", "HackTerminal", "HackCodeClue", "CXLevelExit", "TargetPoint"}
# 구덩이 COL → 걸어 나가는 쪽 이웃 COL (1.1배 뒤 높이차를 44 로 맞춘다)
PITS = {"COL_S2_Roof_PitA": "COL_S2_Roof_R1", "COL_S2_Roof_PitB": "COL_S2_Roof_R4",
        "COL_S4_Yard": "COL_S4_Block_A", "COL_S5_RockPit": "COL_S5_RockB"}
PIT_RISE = 44.0
# 수로 출구(1.1배 뒤 좌표)
P4_Y = (1980.0, 2200.0)
CANAL_FLOOR = -231.0
EXIT_STEPS = [(2140.0, 2185.0, -194.0), (2185.0, 2275.0, -160.0), (2275.0, 2317.0, -120.0), (2317.0, 2358.0, -80.0), (2358.0, 2400.0, -40.0)]
STEP_X = (-140.0, 170.0)
SLAB_X = (-127.0, 157.0)

BRO, SIS = (34.0, 88.0), (34.0, 80.0)
STEP_MAX, JUMP_SAFE = 45.0, 80.0        # MaxStepHeight 45, 최고 점프 90 → 여유 두고 80


def _manifest_path():
    p = globals().get("VZ_MANIFEST_OVERRIDE")
    return p or unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/" + MANIFEST_NAME)


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _v(l):
    return [l.x, l.y, l.z]


def _bounds(a):
    o, e = a.get_actor_bounds(False)
    return [o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z]


def _by_label():
    return {a.get_actor_label(): a for a in _eas().get_all_level_actors()}


def _is_keep(a):
    lab = a.get_actor_label()
    f = str(a.get_folder_path())
    return lab in KEEP_LABELS or lab.startswith(KEEP_PREFIX) or any(f == k or f.startswith(k + "/") for k in KEEP_FOLDERS)


def _sz(z):
    return BASE_Z + (z - BASE_Z) * S


# ------------------------------------------------------------------------------------------------ floor lookup
def _col_ignore(actors):
    return [a for a in actors if not a.get_actor_label().startswith("COL_") or a.get_actor_label().startswith(("COL_Bound", "COL_HackGate"))]


def _trace(world, a, b, ignore):
    h = unreal.SystemLibrary.line_trace_single(world, a, b, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignore, unreal.DrawDebugTrace.NONE, True)
    if not h:
        return None, None
    t = h.to_tuple()
    return t[4].z, t[9]


def _floor_below(world, x, y, z, ignore, reach=400.0):
    xc = max(LANE_CLAMP[0], min(LANE_CLAMP[1], x))     # 레인 안쪽(경계벽·뒤쪽 소품 밖)에서 잰다
    fz, act = _trace(world, unreal.Vector(xc, y, z + 5.0), unreal.Vector(xc, y, z - reach), ignore)
    return fz, (act.get_actor_label() if act else None)


# ------------------------------------------------------------------------------------------------ apply
def _scale_ramp(a):
    l, s = a.get_actor_location(), a.get_actor_scale3d()
    fwd, up = a.get_actor_forward_vector(), a.get_actor_up_vector()
    L, T = s.x * 100.0, s.z * 100.0
    m = l + up * (T / 2.0)
    e0, e1 = m - fwd * (L / 2.0), m + fwd * (L / 2.0)
    e0 = unreal.Vector(e0.x, e0.y, _sz(e0.z)); e1 = unreal.Vector(e1.x, e1.y, _sz(e1.z))
    d = e1 - e0
    L2 = d.length()
    r = a.get_actor_rotation()
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, d.z / L2))))
    a.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=r.yaw, roll=r.roll), False)
    a.set_actor_scale3d(unreal.Vector(L2 / 100.0, s.y, s.z))
    up2 = a.get_actor_up_vector()
    m2 = (e0 + e1) * 0.5
    a.set_actor_location(m2 - up2 * (T / 2.0), False, False)
    return round(r.pitch, 1), round(pitch, 1)


def _set_box(a, x0, x1, y0, y1, z0, z1):
    """COL 큐브(100uu, 회전 없음)를 박스에 맞춘다."""
    a.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
    a.set_actor_scale3d(unreal.Vector((x1 - x0) / 100.0, (y1 - y0) / 100.0, (z1 - z0) / 100.0))
    a.set_actor_location(unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), False, False)


def _fit_actor(a, box):
    """yaw-only 메시 액터를 월드 바운드 box 에 맞춘다(축별 비율로 스케일 → 중심 이동)."""
    b = _bounds(a)
    s = a.get_actor_scale3d()
    yaw = round(a.get_actor_rotation().yaw) % 180
    rx = (box[1] - box[0]) / max(b[1] - b[0], 1e-3)
    ry = (box[3] - box[2]) / max(b[3] - b[2], 1e-3)
    rz = (box[5] - box[4]) / max(b[5] - b[4], 1e-3)
    if yaw == 90:
        a.set_actor_scale3d(unreal.Vector(s.x * ry, s.y * rx, s.z * rz))
    else:
        a.set_actor_scale3d(unreal.Vector(s.x * rx, s.y * ry, s.z * rz))
    b = _bounds(a)
    l = a.get_actor_location()
    a.set_actor_location(unreal.Vector(l.x + (box[0] + box[1] - b[0] - b[1]) / 2, l.y + (box[2] + box[3] - b[2] - b[3]) / 2,
                                       l.z + (box[4] + box[5] - b[4] - b[5]) / 2), False, False)


def _col_like(src, label, box):
    a = _eas().spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector(0, 0, 0))
    c = a.static_mesh_component
    c.set_collision_profile_name("BlockAll")
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property("visible", False)
    c.set_editor_property("hidden_in_game", True)
    c.set_editor_property("cast_shadow", False)
    a.set_folder_path(str(src.get_folder_path()))
    a.set_actor_label(label)
    a.tags = list(src.tags) + [TAG]
    _set_box(a, *box)
    return a


def _mesh_like(src, label, box):
    sc = src.static_mesh_component
    a = _eas().spawn_actor_from_object(sc.static_mesh, src.get_actor_location(), src.get_actor_rotation())
    a.set_actor_scale3d(src.get_actor_scale3d())
    c = a.static_mesh_component
    c.set_collision_profile_name("NoCollision")
    c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    for i, m in enumerate(sc.get_materials()):
        if m:
            c.set_material(i, m)
    a.set_folder_path(str(src.get_folder_path()))
    a.set_actor_label(label)
    a.tags = list(src.tags) + [TAG]
    _fit_actor(a, box)
    return a


def apply():
    world = _world()
    eas = _eas()
    actors = eas.get_all_level_actors()
    if any(TAG in [str(t) for t in a.tags] for a in actors) or (os.path.exists(_manifest_path()) and not globals().get("VZ_MANIFEST_OVERRIDE")):
        print("REFUSED: already applied (marker actor tagged %s or %s exists). Use undo first." % (TAG, _manifest_path()))
        return False
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if dirty and not globals().get("VZ_ALLOW_DIRTY"):
        print("REFUSED: map packages already dirty (someone's unsaved edits):", dirty)
        return False

    ws = world.get_world_settings()
    backup = {"level": LEVEL, "scale": S, "base_z": BASE_Z, "kill_z": ws.get_editor_property("kill_z"), "actors": [], "created": [], "exit_zone": {}}
    for a in actors:
        r = a.get_actor_rotation()
        backup["actors"].append({"name": a.get_name(), "label": a.get_actor_label(), "loc": _v(a.get_actor_location()),
                                 "rot": [r.pitch, r.yaw, r.roll], "scale": _v(a.get_actor_scale3d())})
        if a.get_class().get_name() == "CXLevelExit":
            backup["exit_zone"][a.get_name()] = _v(a.get_editor_property("zone_extent"))

    # 점 액터 바닥(스케일 전 COL 기준)
    ignore = _col_ignore(actors)
    floors = {}
    for a in actors:
        if a.get_class().get_name() in POINT_CLASSES:
            l = a.get_actor_location()
            fz, under = _floor_below(world, l.x, l.y, l.z, ignore)
            floors[a.get_name()] = (fz, under)

    counts = {"mesh_stretched": 0, "ramp": 0, "point_on_floor": 0, "point_z": 0, "light_fx_z": 0, "kept": 0, "approx_rotated": []}
    for a in actors:
        cls = a.get_class().get_name()
        lab = a.get_actor_label()
        if a.get_attach_parent_actor():
            continue          # 부모를 따라 움직임
        l = a.get_actor_location()
        if _is_keep(a):
            counts["kept"] += 1
            continue
        if isinstance(a, unreal.StaticMeshActor):
            r = a.get_actor_rotation()
            if lab.startswith("COL_") and abs(r.pitch) > 0.5:
                print("ramp", lab, "pitch %s -> %s" % _scale_ramp(a))
                counts["ramp"] += 1
                continue
            if abs(r.pitch) > 0.01 or abs(r.roll) > 0.01:
                counts["approx_rotated"].append((lab, round(r.pitch, 1), round(r.roll, 1)))
            s = a.get_actor_scale3d()
            if lab in FIXED_BOTTOM:
                b = _bounds(a)
                _set_box(a, b[0], b[1], b[2], b[3], FIXED_BOTTOM[lab], _sz(b[5]))
            else:
                a.set_actor_scale3d(unreal.Vector(s.x, s.y, s.z * S))
                a.set_actor_location(unreal.Vector(l.x, l.y, _sz(l.z)), False, False)
            counts["mesh_stretched"] += 1
        elif cls in POINT_CLASSES:
            fz, under = floors.get(a.get_name(), (None, None))
            if fz is not None:
                a.set_actor_location(unreal.Vector(l.x, l.y, _sz(fz) + (l.z - fz)), False, False)
                counts["point_on_floor"] += 1
            else:
                a.set_actor_location(unreal.Vector(l.x, l.y, _sz(l.z)), False, False)
                counts["point_z"] += 1
            if cls == "CXLevelExit":
                ze = a.get_editor_property("zone_extent")
                a.set_editor_property("zone_extent", unreal.Vector(ze.x, ze.y, ze.z * S))
        else:
            a.set_actor_location(unreal.Vector(l.x, l.y, _sz(l.z)), False, False)
            if cls == "PostProcessVolume":
                s = a.get_actor_scale3d()
                a.set_actor_scale3d(unreal.Vector(s.x, s.y, s.z * S))
            counts["light_fx_z"] += 1
    ws.set_editor_property("kill_z", KILLZ_NEW)
    print("scaled:", {k: (v if not isinstance(v, list) else len(v)) for k, v in counts.items()}, "approx rotated:", counts["approx_rotated"])

    created = fix_pits() + fix_canal_exit()
    backup["created"] = created
    m = _spawn_marker()
    backup["created"].append(m.get_name())
    backup["counts"] = {k: v for k, v in counts.items()}
    json.dump(backup, open(_manifest_path(), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    print("backup ->", _manifest_path())
    return True


def _spawn_marker():
    eas = _eas()
    cls = unreal.Note if hasattr(unreal, "Note") else unreal.TargetPoint
    m = eas.spawn_actor_from_class(cls, unreal.Vector(0, -700, 300))
    m.set_actor_label(MARKER_LABEL)
    m.set_folder_path("Info")
    m.tags = [TAG]
    try:
        m.set_editor_property("text", "vertical scale x1.1 applied (Tools/LevelBuild/scale_map*_vertical.py). Backup: Docs/LevelManifests/" + MANIFEST_NAME)
    except Exception:
        pass
    return m


def fix_pits():
    by = _by_label()
    created = []
    for pit, nb in PITS.items():
        p, n = by[pit], by[nb]
        pb, nbx = _bounds(p), _bounds(n)
        new_top = nbx[5] - PIT_RISE
        delta = new_top - pb[5]
        if delta <= 0.01:
            continue
        moved, stretched = [], []
        for a in _eas().get_all_level_actors():
            if a == p or a == n or not isinstance(a, unreal.StaticMeshActor) or _is_keep(a):
                continue
            lab = a.get_actor_label()
            if lab.startswith(("COL_Bound", "COL_HackGate", "NEON_HackGate")):
                continue
            b = _bounds(a)
            ov = min(b[3], pb[3]) - max(b[2], pb[2])
            if b[0] >= 180 or ov < 0.8 * max(b[3] - b[2], 1) or b[3] - b[2] > (pb[3] - pb[2]) + 60:
                continue
            l = a.get_actor_location()
            if abs(b[4] - pb[4]) <= 3 and b[5] < pb[5] + 3 and b[5] > pb[5] - 80:
                s = a.get_actor_scale3d()
                k = (b[5] + delta - b[4]) / max(b[5] - b[4], 1e-3)
                a.set_actor_scale3d(unreal.Vector(s.x, s.y, s.z * k))
                nb2 = _bounds(a)
                a.set_actor_location(unreal.Vector(l.x, l.y, l.z + (b[4] - nb2[4])), False, False)
                stretched.append(lab)
            elif abs(b[5] - pb[5]) <= 12 or (abs(b[4] - pb[5]) <= 3 and b[5] - b[4] < 200):
                a.set_actor_location(unreal.Vector(l.x, l.y, l.z + delta), False, False)
                moved.append(lab)
        _set_box(p, pb[0], pb[1], pb[2], pb[3], pb[4], new_top)
        print("pit", pit, "top %.1f -> %.1f (rise to %s %.1f)" % (pb[5], new_top, nb, PIT_RISE), "moved", moved, "stretched", stretched)
    return created


def fix_canal_exit():
    by = _by_label()
    created = []
    # P4 줄이기(COL + 판자 아트 + 끝 체인)
    p4 = by["COL_S1_Pallet_P4"]
    b = _bounds(p4)
    _set_box(p4, b[0], b[1], P4_Y[0], P4_Y[1], b[4], b[5])
    for a in _eas().get_all_level_actors():
        f = str(a.get_folder_path())
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        ab = _bounds(a)
        if f.endswith("/Pallets") and ab[2] > 1950 and ab[3] < 2260:
            _fit_actor(a, [ab[0], ab[1], P4_Y[0], P4_Y[1], ab[4], ab[5]])
            print("pallet art", a.get_actor_label(), "-> Y %.0f..%.0f" % P4_Y)
        elif f.endswith("/Chains") and 2150 < (ab[2] + ab[3]) / 2 < 2260:
            l = a.get_actor_location()
            a.set_actor_location(unreal.Vector(l.x, P4_Y[1] - 30.0, l.z), False, False)
            print("chain", a.get_actor_label(), "-> Y", P4_Y[1] - 30.0)
    # 계단 COL 5단
    steps = sorted([a for a in by.values() if a.get_actor_label().startswith("COL_S1_ExitStep_")], key=lambda a: a.get_actor_label())
    for i, (y0, y1, top) in enumerate(EXIT_STEPS):
        box = (STEP_X[0], STEP_X[1], y0, y1, CANAL_FLOOR, top)
        if i < len(steps):
            _set_box(steps[i], *box)
        else:
            a = _col_like(steps[-1], "COL_S1_ExitStep_%d" % (i + 1), box)
            created.append(a.get_name())
    # 돌판 아트 5장(윗면 = 계단 윗면, 두께 68×1.1)
    slabs = sorted([a for a in _eas().get_all_level_actors() if isinstance(a, unreal.StaticMeshActor) and str(a.get_folder_path()).endswith("/ExitSteps")],
                   key=lambda a: _bounds(a)[2])
    for i, (y0, y1, top) in enumerate(EXIT_STEPS):
        box = [SLAB_X[0], SLAB_X[1], y0 - 2.0, y1 + 2.0, top - 68.0 * S, top]
        if i < len(slabs):
            _fit_actor(slabs[i], box)
        else:
            a = _mesh_like(slabs[-1], "SM_Cave_Rock_Flat_ExitStep%d" % (i + 1), box)
            created.append(a.get_name())
    print("canal exit: P4 Y %.0f..%.0f, steps" % P4_Y, EXIT_STEPS, "created", len(created))
    return created


def undo():
    path = _manifest_path()
    if not os.path.exists(path):
        print("no backup:", path)
        return
    bk = json.load(open(path, encoding="utf-8"))
    eas = _eas()
    acts = {a.get_name(): a for a in eas.get_all_level_actors()}
    n_del = 0
    for nm in bk["created"]:
        if nm in acts:
            eas.destroy_actor(acts[nm]); n_del += 1
    for a in eas.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
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
        if e["name"] in bk.get("exit_zone", {}):
            a.set_editor_property("zone_extent", unreal.Vector(*bk["exit_zone"][e["name"]]))
        n += 1
    _world().get_world_settings().set_editor_property("kill_z", bk["kill_z"])
    os.rename(path, path + ".undone")
    print("undo: restored", n, "actors, deleted", n_del, "created; KillZ", bk["kill_z"], "— save the level to keep")


# ------------------------------------------------------------------------------------------------ validate
def make_tools(world, ignore):
    """캡슐 겹침(free) / 아래 트레이스(down) / 걷기 시뮬레이션(walk). scale_map1_vertical.py 도 쓴다."""
    def free(x, y, zf, cap):
        r, hh = cap
        hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(x, y, zf + hh + 1.5), r, hh,
                                                           [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], None, ignore)
        return not hits, [h.get_actor_label() for h in (hits or [])]

    def down(x, y, z_from, depth=2500.0):
        return _trace(world, unreal.Vector(x, y, z_from), unreal.Vector(x, y, z_from - depth), ignore)

    def walk(x, y0, y1, z0, cap, dy=10.0, jumps=True, max_jump_gap=260.0):
        """캡슐을 x 고정, y0→y1 로 걷게 한다. 계단(≤45)은 걸어 오르고, 높으면 점프(≤80, 머리 공간 확인), 앞이 낭떠러지면 앞쪽 200cm 안 발판으로 점프."""
        d = 1.0 if y1 > y0 else -1.0
        z, _ = down(x, y0, z0 + 50)
        y = y0
        ev = []
        while (y1 - y) * d > dy:
            yn = y + d * dy
            # 캡슐 발 밑 전체(뒤 가장자리 ~ 앞 가장자리)에서 가장 높은 바닥 = 실제 자동 계단 오르기/턱 끝에 걸쳐 서기와 같은 판정
            fz, fa = None, None
            for off in (-(cap[0] - 3.0), -cap[0] * 0.5, 0.0, cap[0] * 0.5, cap[0] - 3.0):
                tz_, ta_ = down(x, yn + d * off, z + STEP_MAX + 0.5)
                if tz_ is not None and tz_ > z + STEP_MAX + 0.45:
                    tz_ = z + 9999.0          # 트레이스가 상자 안에서 시작(시작점이 그대로 잡힘) = 자동 오르기(45)보다 높은 턱
                if tz_ is not None and (fz is None or tz_ > fz):
                    fz, fa = tz_, ta_
            if fz is not None and fz <= z + STEP_MAX + 0.6 and fz >= z - 30 and free(x, yn, fz, cap)[0]:
                y, z = yn, fz
                continue
            if fz is not None and fz < z - 30 and jumps:   # (턱이면 fz 가 z+9999 라 여기로 오지 않음)
                # 낭떠러지: 앞쪽 같은 높이 이상(≤ +80) 발판으로 점프 가능한지
                cands = []
                g = dy
                while g <= max_jump_gap:
                    yy = y + d * (g + dy)
                    tz, ta = down(x, yy, z + JUMP_SAFE + 1)
                    if tz is not None and tz >= z - 260 and free(x, yy, tz, cap)[0] and free(x, y, z + max(0.0, tz - z) + 10, cap)[0]:
                        reach = 450.0 * (420.0 + math.sqrt(max(0.0, 420.0 ** 2 - 1960.0 * max(0.0, tz - z)))) / 980.0
                        if g + 2.0 * cap[0] <= reach:     # 뒤 가장자리 기준이라 지름만큼 보수적으로
                            cands.append((tz, g, yy, ta))
                    g += dy
                if cands:
                    # 가장 높은 발판(구덩이보다 건너편 지붕)을 고르고, 같은 높이면 가까운 쪽
                    best = max(cands, key=lambda c: (round(c[0]), -c[1]))
                    tz, g, yy, ta = best
                    ev.append(("jump_gap", round(y), round(g), round(tz - z, 1), ta.get_actor_label() if ta else None))
                    y, z = yy, tz
                    continue
                # 떨어진 뒤 캡슐이 들어갈 수 있는 첫 위치(뒤쪽 턱과 겹치지 않게 반경만큼 더 나아감)
                yy = yn
                while abs(yy - yn) <= cap[0] + dy:
                    dz_, da_ = down(x, yy, z + 5)
                    if dz_ is not None and free(x, yy, dz_, cap)[0]:
                        fz, fa = dz_, da_
                        break
                    yy += d * 2.0
                ev.append(("drop", round(yy), round(z - fz, 1), fa.get_actor_label() if fa else None))
                y, z = yy, fz
                continue
            if fz is not None and fz >= z - 30:
                # 바닥은 있는데 캡슐이 막힘(머리/벽)
                pass
            # 벽/턱: 캡슐 앞쪽 발 밑에서 점프 높이(80) 안의 윗면 찾기(그보다 높으면 트레이스가 상자 안에서 시작 → 못 오름)
            tz, ta = None, None
            for off in (0.0, cap[0] * 0.5, cap[0] - 3.0):
                t2, a2 = down(x, yn + d * off, z + JUMP_SAFE + 1.0)
                if t2 is not None and t2 > z + JUMP_SAFE + 0.95:
                    t2, a2 = z + 9999.0, a2
                if t2 is not None and (tz is None or t2 > tz):
                    tz, ta = t2, a2
            ok_head, who = free(x, y, z + (tz - z if tz else 0) + 2, cap) if tz is not None else (False, [])
            if tz is not None and 0 < tz - z <= JUMP_SAFE and jumps and ok_head and free(x, yn, tz, cap)[0]:
                ev.append(("jump_up", round(yn), round(tz - z, 1), ta.get_actor_label() if ta else None))
                y, z = yn, tz
                continue
            ev.append(("BLOCKED", round(yn), round(z, 1), None if tz is None else round(tz - z, 1), free(x, yn, z, cap)[1][:3]))
            return False, ev, y, z
        return True, ev, y, z
    return free, down, walk


def validate():
    world = _world()
    actors = _eas().get_all_level_actors()
    by = {a.get_actor_label(): a for a in actors}
    ignore = _col_ignore(actors)
    fails = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            fails.append(msg)

    free, down, walk = make_tools(world, ignore)

    # 1) 수로 출구: 수로 바닥 여러 곳에서 걸어서(점프 없이) Y 2600 까지
    for cap, nm in ((BRO, "brother"), (SIS, "sister")):
        # 수로 바닥 돌(X 4~116)이 뒤쪽을 막으므로 뒤쪽 줄(X 60, 110)은 마지막 돌 뒤(Y 2180)에서 출발
        # 끝 Y 2520: S2 바닥(0)에 올라선 지점 — 그 뒤 Y 2550 부터는 지붕 계단(X -20~150)·봉인벽이라 줄마다 다름
        for x, starts in ((-85.0, (700.0, 1500.0, 2150.0)), (-60.0, (700.0, 1500.0, 2150.0)), (-38.0, (700.0, 2150.0)), (60.0, (2180.0,)), (110.0, (2180.0,))):
            for ys in starts:
                ok, ev, y, z = walk(x, ys, 2520.0, -150.0, cap, jumps=False)
                ups = [e for e in ev if e[0] != "drop"]
                check(ok and abs(z) < 1 and not ups, f"canal exit {nm} X{x:.0f} from Y{ys:.0f}: reached Y{y:.0f} z{z:.1f} {ev[:3]}")
    # 수로 출구 계단 단 높이
    tops = [CANAL_FLOOR] + [t for _, _, t in EXIT_STEPS] + [0.0]
    check(max(b - a for a, b in zip(tops, tops[1:])) <= STEP_MAX - 3, f"canal exit step rises {[round(b - a, 1) for a, b in zip(tops, tops[1:])]} <= {STEP_MAX - 3}")

    # 2) 경로 걷기(퍼즐 게이트는 열린 것으로 — 게이트 COL 은 ignore 에 들어 있음)
    routes = {
        # 소품 COL(뒤쪽 X 76~150)·계단 봉인벽(X 10~20)을 피하는 줄: S3 1층은 X 40, 계단 A/B 는 X 60, S4/S5 는 무대·엄폐물·바위 앞 X -40
        "sister": [(-70.0, -700.0, 5950.0), (40.0, 5950.0, 6950.0), (60.0, 6950.0, 9380.0), (-40.0, 9450.0, 14100.0)],
        "brother": [(30.0, -700.0, 6950.0), (60.0, 6950.0, 9380.0), (-40.0, 9450.0, 14100.0)],
    }
    for who, segs in routes.items():
        cap = SIS if who == "sister" else BRO
        z0 = 0.0
        for (x, ya, yb) in segs:
            if ya > 9400 and yb > 9400:
                z0 = _sz(728.0)
            ok, ev, y, z = walk(x, ya, yb, z0 + 10, cap)
            jumps = [e for e in ev if e[0].startswith("jump")]
            check(ok, f"route {who} X{x:.0f} Y{ya:.0f}->{yb:.0f}: reached Y{y:.0f} z{z:.1f}, jumps {jumps}, drops {[e for e in ev if e[0] == 'drop']}{'' if ok else ' LAST ' + str(ev[-1:])}")
            z0 = z

    # 3) 구덩이에서 걸어 나오기(점프 없이)
    for pit, nb in PITS.items():
        pb, nbb = _bounds(by[pit]), _bounds(by[nb])
        x = 60.0 if pit.startswith("COL_S2") else 0.0   # S2 지붕은 뒤쪽(X -20~170), 처마 차단벽(X -40~-20)을 피해서
        yc = (pb[2] + pb[3]) / 2
        target = nbb[2] + 60 if nbb[2] >= pb[3] - 1 else nbb[3] - 60
        for cap, nm in ((BRO, "brother"), (SIS, "sister")):
            ok, ev, y, z = walk(x, yc, target, pb[5] + 5, cap, jumps=False)
            check(ok and abs(z - nbb[5]) < 1, f"pit walk-out {nm} {pit} ({pb[5]:.1f}) -> {nb} ({nbb[5]:.1f}): z{z:.1f} {ev[:2]}")

    # 4) 여동생 처마 통로: 여동생 통과 / 오빠 불가
    for y in (3200.0, 4300.0, 5400.0):
        check(free(-70.0, y, 0.0, SIS)[0] and not free(-70.0, y, 0.0, BRO)[0], f"sister duct Y{y:.0f}: sister fits, brother blocked")

    # 5) 경사 각도
    for a in actors:
        lab = a.get_actor_label()
        if lab.startswith("COL_") and abs(a.get_actor_rotation().pitch) > 0.5:
            check(abs(a.get_actor_rotation().pitch) < 40.0, f"ramp {lab} pitch {a.get_actor_rotation().pitch:.1f} < 40 (walkable 44.8)")

    # 6) 해킹 게이트: 막힘 + 위로 못 넘음
    for a in actors:
        lab = a.get_actor_label()
        if not lab.startswith("COL_HackGate"):
            continue
        b = _bounds(a)
        yc = (b[2] + b[3]) / 2
        fz, _ = down(-60.0, b[2] - 60, b[5])
        ign2 = [i for i in ignore if i != a]
        h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(-60, b[2] - 120, fz + 90), unreal.Vector(-60, b[3] + 120, fz + 90),
                                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ign2, unreal.DrawDebugTrace.NONE, True)
        blk = h.to_tuple()[9].get_actor_label() if h else None
        cap_hit = _trace(world, unreal.Vector(-60, yc, b[5] + 2), unreal.Vector(-60, yc, b[5] + 400), ignore)[1]
        capped = cap_hit is not None and cap_hit.get_actor_label().startswith("COL_")
        check(blk == lab and b[4] <= fz + 1 and (b[5] - fz >= 300 or capped) and b[0] <= -124 and b[1] >= 154,
              f"gate {lab}: blocks path ({blk}), Z {b[4]:.0f}..{b[5]:.0f} floor {fz:.0f}, capped {cap_hit.get_actor_label() if capped else None}")

    # 7) 점 액터가 바닥에 붙어 있음
    for a in actors:
        cls = a.get_class().get_name()
        if cls not in ("SpiderBot", "HackTerminal", "CXLevelExit", "PlayerStart"):
            continue
        l = a.get_actor_location()
        fz, under = _floor_below(world, l.x, l.y, l.z, ignore)
        expect = {"SpiderBot": 52.0, "HackTerminal": 0.0, "CXLevelExit": 0.0, "PlayerStart": None}[cls]
        off = None if fz is None else l.z - fz
        ok = off is not None and (expect is None and 75 < off < 100 or expect is not None and abs(off - expect) < 2.5)
        check(ok, f"{cls} {a.get_actor_label()} at z{l.z:.1f}, floor {None if fz is None else round(fz, 1)} ({under}), offset {None if off is None else round(off, 1)}")

    # 8) KillZ 가 모든 것보다 아래
    kz = world.get_world_settings().get_editor_property("kill_z")
    lowest = min(_bounds(a)[4] for a in actors if isinstance(a, unreal.StaticMeshActor))
    check(kz < lowest - 200, f"KillZ {kz:.0f} below lowest geometry {lowest:.0f}")
    print("VALIDATE:", "ALL PASS" if not fails else f"{len(fails)} FAIL")
    return fails


def run():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    cur = les.get_current_level().get_outermost().get_name() if les.get_current_level() else ""
    if cur != LEVEL:
        unreal.SystemLibrary.collect_garbage()
        print("load_level:", les.load_level(LEVEL), "— run this script again")
        return
    mode = globals().get("VZ_MODE", "apply")
    if mode == "undo":
        undo()
        return
    if mode == "validate":
        validate()
        return
    if not apply():
        return
    fails = validate()
    if fails:
        print("NOT SAVED — validation failed (undo() restores, or fix and save manually)")
    elif globals().get("VZ_NO_SAVE"):
        print("NOT SAVED — VZ_NO_SAVE")
    else:
        print("saved:", les.save_current_level())


if __name__ == "__main__" or globals().get("VZ_AUTORUN", True):
    run()
