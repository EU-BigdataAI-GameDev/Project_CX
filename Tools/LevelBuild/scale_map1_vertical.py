"""
Map1_SoulCity1 세로(Z) 1.1배 (2026-11-04, 사용자 요청 — 기존 액터 변환을 고치는 전체 레벨 편집).

- 규칙은 scale_map2_vertical.py 와 같다(그 파일의 apply/undo/make_tools 를 그대로 쓰고 설정만 바꾼다):
    BASE_Z = 0 (S1/S2/S5 지상 COL 윗면). 정적 메시(아트·COL·벽 메우기·네온)는 위치 Z·Z 스케일 × 1.1, COL 경사 4개는 끝점 1.1배로 재계산,
    점 액터(PlayerStart, 거미, 단말기, 단서, 레벨 출구, 마커)는 바닥에서의 높이 유지, 조명/포그/PPV 는 위치 Z × 1.1, 레벨 출구 ZoneExtent.Z × 1.1.
- Map1 에는 여동생 전용 통로·낮은 천장이 없다(분기 높이 예외 없음). 구덩이도 수로(-40 → -44, 걸어 나옴 44 ≤ 45)뿐이라 고칠 것 없음.
- KillZ -1500 → -1800 (가장 낮은 메시가 -1409 → -1550 이 되므로 여유 250).
- 필수 점프: S1 데크 +50 → +55, S3 단차 발판 +55 → +60.5 × 3 (최고 점프 90 안). 테라스 182 → 200.2, 코어 711 → 782.1.
- 백업 Docs/LevelManifests/Map1_vertical_scale_1.1_backup.json, 표식 액터 태그 GEN_Map2VScale(두 맵 공용 태그 — fill_wall_holes / add_map1_hacking 이 이 태그로 1.1배 여부를 안다),
  두 번 실행 거부, 실행 전 dirty 면 거부, 검사 실패 시 저장 안 함. 되돌리기: globals()["VZ_MODE"] = "undo".

실행(에디터 원격 실행): 1회차 — 현재 레벨이 Map1 이 아니면 불러오고 끝. 2회차 — 적용 + 검사 + 저장.
"""
import unreal, os

_LIB = os.path.join(os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else
                    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Tools/LevelBuild"), "scale_map2_vertical.py")

LEVEL1 = "/Game/Project_CX/Maps/Map1_SoulCity1"
BRO, SIS = (34.0, 88.0), (34.0, 80.0)


def _lib():
    g = {"__name__": "vz_lib", "VZ_AUTORUN": False}
    for k in ("VZ_MANIFEST_OVERRIDE", "VZ_ALLOW_DIRTY"):
        if k in globals():
            g[k] = globals()[k]
    exec(open(_LIB, encoding="utf-8").read(), g)
    g.update(LEVEL=LEVEL1, KEEP_LABELS=set(), KEEP_PREFIX=("__none__",), KEEP_FOLDERS=(), FIXED_BOTTOM={}, PITS={},
             KILLZ_NEW=-1800.0, MANIFEST_NAME="Map1_vertical_scale_1.1_backup.json", MARKER_LABEL="INFO_Map1_VerticalScale_x1.1",
             LANE_CLAMP=(-200.0, 200.0))
    g["fix_canal_exit"] = lambda: []
    g["fix_pits"] = lambda: []
    return g


def validate1(g):
    world = g["_world"]()
    actors = g["_eas"]().get_all_level_actors()
    ignore = g["_col_ignore"](actors)
    free, down, walk = g["make_tools"](world, ignore)
    fails = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            fails.append(msg)

    S = g["S"]
    # 경로 걷기(게이트는 열린 것으로). 오빠 = 앞길(지상), 여동생 = S2 뒷테라스. 소품 COL(레인 뒤쪽·앞쪽 엄폐물)을 피하는 줄.
    routes = {
        # 앞길 X -60(쓰레기통 X -212~-118 과 판잣집 벽 X -9~29 사이), S4/S5 는 엄폐물(X -120~0)·물탱크(X -37~)를 피해 X -200
        # 여동생 테라스 X 90(뒤쪽 쓰레기 봉투 X 144~ 앞), 계단 밑 바위(Y ~3097)를 지난 Y 3140 부터
        "brother": [(-60.0, -600.0, 8300.0), (-200.0, 8300.0, 12500.0)],
        "sister": [(-60.0, -600.0, 3050.0), (90.0, 3140.0, 6150.0), (-60.0, 6150.0, 8300.0), (-200.0, 8300.0, 12500.0)],
    }
    for who, segs in routes.items():
        cap = SIS if who == "sister" else BRO
        z0 = 0.0
        for (x, ya, yb) in segs:
            ok, ev, y, z = walk(x, ya, yb, z0 + 60, cap)
            jumps = [e for e in ev if e[0].startswith("jump")]
            check(ok, f"route {who} X{x:.0f} Y{ya:.0f}->{yb:.0f}: reached Y{y:.0f} z{z:.1f}, jumps {jumps}, drops {[e for e in ev if e[0] == 'drop']}{'' if ok else ' LAST ' + str(ev[-1:])}")
            z0 = z
    # 걸어 나오기(점프 없이): S1 수로 → 양쪽 지상, S5 발판 밑 지상 → 출구
    for cap, nm in ((BRO, "brother"), (SIS, "sister")):
        for (x, ya, yb, zh) in ((0.0, 2200.0, 2500.0, -40 * S), (0.0, 2200.0, 1900.0, -40 * S), (0.0, 10100.0, 12500.0, 0.0)):
            ok, ev, y, z = walk(x, ya, yb, zh + 5, cap, jumps=False)
            check(ok and abs(z) < 1, f"walk-out {nm} X{x:.0f} Y{ya:.0f}->{yb:.0f}: z{z:.1f} {ev[:2]}")
    # 경사 각도
    for a in actors:
        lab = a.get_actor_label()
        if lab.startswith("COL_") and abs(a.get_actor_rotation().pitch) > 0.5:
            check(abs(a.get_actor_rotation().pitch) < 40.0, f"ramp {lab} pitch {a.get_actor_rotation().pitch:.1f} < 40")
    # 해킹 게이트
    for a in actors:
        lab = a.get_actor_label()
        if not lab.startswith("COL_HackGate"):
            continue
        b = g["_bounds"](a)
        fz = max((down(-60.0, yy, b[5])[0] or -1e9) for yy in (b[2] - 60, b[3] + 60))   # 게이트 앞/뒤 중 높은 바닥(앞이 계단일 수 있음)
        ign2 = [i for i in ignore if i != a]
        h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(-60, b[2] - 120, fz + 90), unreal.Vector(-60, b[3] + 120, fz + 90),
                                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ign2, unreal.DrawDebugTrace.NONE, True)
        blk = h.to_tuple()[9].get_actor_label() if h else None
        check(blk == lab and b[4] <= fz + 1 and b[5] - fz >= 300 and b[0] <= -249 and b[1] >= 249,
              f"gate {lab}: blocks path ({blk}), Z {b[4]:.0f}..{b[5]:.0f} floor {fz:.0f}")
    # 점 액터 바닥
    for a in actors:
        cls = a.get_class().get_name()
        if cls not in ("SpiderBot", "HackTerminal", "CXLevelExit", "PlayerStart"):
            continue
        l = a.get_actor_location()
        fz, under = g["_floor_below"](world, l.x, l.y, l.z, ignore)
        expect = {"SpiderBot": 52.0, "HackTerminal": 0.0, "CXLevelExit": 0.0, "PlayerStart": None}[cls]
        off = None if fz is None else l.z - fz
        ok = off is not None and (expect is None and 75 < off < 100 or expect is not None and abs(off - expect) < 2.5)
        check(ok, f"{cls} {a.get_actor_label()} at z{l.z:.1f}, floor {None if fz is None else round(fz, 1)} ({under}), offset {None if off is None else round(off, 1)}")
    kz = world.get_world_settings().get_editor_property("kill_z")
    lowest = min(g["_bounds"](a)[4] for a in actors if isinstance(a, unreal.StaticMeshActor))
    check(kz < lowest - 200, f"KillZ {kz:.0f} below lowest geometry {lowest:.0f}")
    print("VALIDATE:", "ALL PASS" if not fails else f"{len(fails)} FAIL")
    return fails


def run1():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    cur = les.get_current_level().get_outermost().get_name() if les.get_current_level() else ""
    if cur != LEVEL1:
        unreal.SystemLibrary.collect_garbage()
        print("load_level:", les.load_level(LEVEL1), "— run this script again")
        return
    g = _lib()
    mode = globals().get("VZ_MODE", "apply")
    if mode == "undo":
        g["undo"]()
        return
    if mode == "validate":
        validate1(g)
        return
    if not g["apply"]():
        return
    fails = validate1(g)
    if fails:
        print("NOT SAVED — validation failed (VZ_MODE='undo' restores, or fix and save manually)")
    elif globals().get("VZ_NO_SAVE"):
        print("NOT SAVED — VZ_NO_SAVE")
    else:
        print("saved:", les.save_current_level())


if globals().get("VZ_AUTORUN", True):
    run1()
