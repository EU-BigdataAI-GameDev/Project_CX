"""
Map2_SoulCity2 계단 그래픽 ↔ 걷기 콜라이더 맞추기 (2026-11-07, .claude/Plans/맵2 개선 기획.md 2장).

문제
  - 계단 아트(SM_stairs_W1_H3_00 을 비균등 스케일)는 위쪽 23% 가 평평한 계단참이고 나머지가 10단 계단이다(첫 단 높이 = 메시 높이의 10%).
    그런데 경사 콜라이더(build_map2.ramp)는 아트 바운드의 아래 모서리 → 위 모서리를 곧게 이어서, 계단참 시작점에서 콜라이더가
    계단 코보다 최대 66cm(S3 는 92cm) 아래에 있다 → 발이 계단 속에 파묻힌다.
  - S5 바위 턱 아트 SM_Slums_Wall_02o 는 이름과 달리 "계단참 + 계단 + 계단참 + 계단" 모양의 계단 벽이다.
    마지막 단말기(HackTerminal_M2_Memory, Y 12505) 아래 바위 A 는 화면에 211 → 7cm 로 내려가는 계단이 보이는데
    COL_S5_RockA 는 높이 220 의 평평한 상자였다(발이 계단 위 최대 2m 허공에 뜸). 바위 B 도 같은 메시(낮게 눌린 경사).

방법(그래픽이 기준)
  - 메시 로컬 단면(정점에서 뽑은 디딤판 목록, 아래 PROFILES)을 아트 액터 변환으로 월드에 옮겨,
    계단참 = 상자, 계단 = 계단 코를 잇는 경사(30~39°, 걸을 수 있는 각 44.8° 이하), 맨 아래 첫 단 = 상자(자동 오르기 45 이하)로 만든다.
  - 기존 경사 COL 은 같은 액터를 옮겨 다시 쓰고(이름 유지), 평평한 바닥 COL(RockA/RockB)은 위쪽 계단참 길이로 잘라 낸다.
    새로 만든 COL 은 기존 COL 을 복제해 충돌 설정(BlockAll·카메라 무시·숨김)을 그대로 쓴다.
  - 바위 A 아래 끝이 7cm 로 내려가므로, 그 끝에 있는 메모리 게이트(HackGate_M2_S5Pit)의 네온 막대·아래 레일·COL 아래 끝을 새 바닥까지 내린다
    (원래 막대 아래 끝 220 → 그대로 두면 캐릭터가 막대 밑으로 지나갈 것처럼 보이는데 보이지 않는 벽에 막힌다).
  - 바위 구덩이(COL_S5_RockPit / SM_Slums_Wall_02o2)는 고치지 않는다: 아트대로면 구덩이 끝 높이 2 → 바위 B 시작 112(+110, 점프 90 초과)로 진행 불가.
  - 고치거나 만든 액터에는 태그 GEN_Map2StairFix(만든 것은 GEN_Map2StairFix_Added 도)를 붙인다.

안전
  - 실행 전 Map2 가 dirty 면 거부. 이미 적용됐으면(태그 액터 또는 백업 파일) 거부.
  - 원래 변환·태그는 Docs/LevelManifests/Map2_stairs_fix_backup.json 에 기록 → MODE "undo" 로 되돌린다(만든 액터 삭제 + 변환/태그 복원).
  - 적용 후 검사(라인 트레이스, 캡슐 서기) 실패면 저장하지 않는다.

실행(에디터 원격 실행, 현재 레벨 Map2_SoulCity2):
  globals()["STAIR_MODE"] = "audit" | "apply" | "apply2" | "undo" | "verify" ;   (apply2 = 2단계: 구덩이 아트 교체 + 단말기를 경사 위로) exec(open(<이 파일>).read())
  audit/verify 는 바꾸지 않고 트레이스 결과만 Docs/LevelManifests/map2_stairs_audit.json(phase=before/after)에 쓴다.
"""
import unreal, json, math, os

LEVEL = "/Game/Project_CX/Maps/Map2_SoulCity2"
TAG = "GEN_Map2StairFix"
TAG_ADDED = "GEN_Map2StairFix_Added"
MAN_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/")
BACKUP = MAN_DIR + "Map2_stairs_fix_backup.json"
AUDIT = MAN_DIR + "map2_stairs_audit.json"

TOL_Z, TOL_Y = 15.0, 30.0
WALKABLE = 44.8
RAMP_T = 20.0

# ---------------------------------------------------------------- 메시 로컬 단면 (정점 덤프에서 뽑음, 로컬 Y/Z)
# treads: (y0, y1, z) 디딤판 윗면. walk: 콜라이더로 만들 조각 — ("box", y0, y1, z_top) / ("ramp", (y,z), (y,z)). floor_z: 메시 바닥.
_ST_TREADS = [(-155.8, -84.0, 90.9)] + [(-84.0 + 26.62 * i, -84.0 + 26.62 * (i + 1), 72.7 - 18.18 * i) for i in range(9)]
_W2O_TREADS = ([(-385.8, -240.2, 300.0)] + [(-240.2 + 15.165 * (k - 1), -240.2 + 15.165 * k, 300.0 - 18.18 * k) for k in range(1, 24)]
               + [(108.4, 231.8, -136.4)] + [(231.8 + 22.0 * (k - 1), 231.8 + 22.0 * k, z) for k, z in
                                             enumerate([-156.8, -177.3, -200.0, -220.0, -240.0, -260.0, -280.0], 1)])
PROFILES = {
    "SM_stairs_W1_H3_00": {
        "floor_z": -90.9, "treads": _ST_TREADS,
        "walk": [("box", -155.8, -84.0, 90.9, "Landing"), ("ramp", (-84.0, 90.9), (155.8, -72.7), "RAMP"),
                 ("box", 129.2, 155.8, -72.7, "FirstStep")],
        "nosing": [(-155.8, 90.9), (-84.0, 90.9), (155.8, -72.7)],
    },
    "SM_Slums_Wall_02o": {
        "floor_z": -300.0, "treads": _W2O_TREADS,
        "walk": [("box", -385.8, -240.2, 300.0, "FLAT"), ("ramp", (-240.2, 300.0), (123.5, -136.4), "Ramp1"),
                 ("box", 108.4, 231.8, -136.4, "MidLanding"), ("ramp", (231.8, -136.4), (385.8, -280.0), "Ramp2")],
        "nosing": [(-385.8, 300.0), (-240.2, 300.0), (123.5, -136.4), (231.8, -136.4), (385.8, -280.0)],
    },
}

# ---------------------------------------------------------------- 계단 목록 (아트 라벨 → 짝 콜라이더)
# ramp: 다시 쓸 기존 경사 COL / flat: 잘라 낼 평평한 COL. under: 상자 아래 끝(None = 메시 바닥까지 꽉 채움).
STAIRS = [
    {"id": "S1_ExitSteps", "art": None, "note": "수로 출구 돌계단 5단 — 돌판 아트 5장과 단 상자 COL 5개(이미 일치, 검사만)",
     "pairs": [("SM_Cave_Rock_Flat01", "COL_S1_ExitStep_1"), ("SM_Cave_Rock_Flat2", "COL_S1_ExitStep_2"), ("SM_Cave_Rock_Flat3", "COL_S1_ExitStep_3"),
               ("SM_Cave_Rock_Flat4", "COL_S1_ExitStep_4"), ("SM_Cave_Rock_Flat_ExitStep5", "COL_S1_ExitStep_5")]},
    {"id": "S2_StairUp", "art": "SM_stairs_W1_H3_00", "ramp": "COL_S2_StairUp", "under": 20.0, "fix": True},
    {"id": "S2_StairDown", "art": "SM_stairs_W1_H3_1", "ramp": "COL_S2_StairDown", "under": 20.0, "fix": True},
    {"id": "S3_StairA", "art": "SM_stairs_W1_H3_2", "ramp": "COL_S3_StairA", "under": 30.0, "fix": True},
    {"id": "S3_StairB", "art": "SM_stairs_W1_H3_3", "ramp": "COL_S3_StairB", "under": 30.0, "fix": True},
    {"id": "S5_Stair", "art": "SM_stairs_W1_H3_4", "ramp": "COL_S5_Stair", "under": 20.0, "fix": True},
    {"id": "S5_RockA", "art": "SM_Slums_Wall_02o", "flat": "COL_S5_RockA", "under": None, "ramp_t": 30.0, "fix": True,
     "note": "마지막 단말기(CAVE_LIFT_ECHO) 아래 계단 — 평평한 COL 220 이 계단을 덮고 있었음"},
    {"id": "S5_RockPit", "art": "SM_Slums_Wall_02o2", "flat": "COL_S5_RockPit", "fix": False, "flat_art": "SM_Slums_Wall_02k_S5RockPit",
     "note": "콜라이더는 그대로(77). 아트대로면 구덩이 끝 2 → 바위 B 112(+110 > 점프 90) 진행 불가 → 2단계(STAIR_MODE='apply2')에서 아트를 윗면이 평평한 SM_Slums_Wall_02k 로 교체"},
    {"id": "S5_RockB", "art": "SM_Slums_Wall_02o3", "flat": "COL_S5_RockB", "under": None, "ramp_t": 30.0, "fix": True,
     "note": "낮게 눌린 계단 벽(단 3~4cm, 경사로처럼 보임) — 평평한 COL 121 을 아트 경사에 맞춤"},
]
# 바위 A 끝(계단 아래)에 있는 게이트: 막대·레일·COL 아래 끝을 새 바닥 높이로
GATE = {"stair": "S5_RockA", "col": "COL_HackGate_M2_S5Pit", "bar_prefix": "NEON_HackGate_M2_S5Pit_", "rail_prefix": "NEON_HackGate_M2_S5Pit_Rail_",
        "rail_bottom_label": "NEON_HackGate_M2_S5Pit_Rail_204", "col_bottom": 0.0}


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _by_label():
    out = {}
    for a in _eas().get_all_level_actors():
        out.setdefault(a.get_actor_label(), a)
    return out


def _tags(a):
    return [str(t) for t in a.tags]


def _bounds(a):
    o, e = a.get_actor_bounds(False)
    return [o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z]


def _xf(a):
    l, r, s = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
    return {"loc": [l.x, l.y, l.z], "rot": [r.pitch, r.yaw, r.roll], "scale": [s.x, s.y, s.z]}


def _set_xf(a, xf):
    a.set_actor_rotation(unreal.Rotator(pitch=xf["rot"][0], yaw=xf["rot"][1], roll=xf["rot"][2]), False)
    a.set_actor_scale3d(unreal.Vector(*xf["scale"]))
    a.set_actor_location(unreal.Vector(*xf["loc"]), False, False)


def _r(v, n=1):
    return [round(x, n) for x in v] if isinstance(v, (list, tuple)) else round(v, n)


# ---------------------------------------------------------------- 아트 단면 → 월드
class Art:
    def __init__(self, actor):
        self.a = actor
        self.mesh = actor.static_mesh_component.static_mesh.get_name()
        self.p = PROFILES[self.mesh]
        self.t = actor.get_actor_transform()
        b = _bounds(actor)
        self.x0, self.x1 = b[0], b[1]

    def w(self, ly, lz):
        v = self.t.transform_location(unreal.Vector(0.0, ly, lz))
        return v.y, v.z

    def treads(self):
        out = []
        for y0, y1, z in self.p["treads"]:
            (a, za), (b, _) = self.w(y0, z), self.w(y1, z)
            out.append((min(a, b), max(a, b), za))
        return sorted(out)

    def tread_z(self, y):
        best = None
        for y0, y1, z in self.treads():
            if y0 - 0.5 <= y <= y1 + 0.5:
                best = z if best is None else max(best, z)
        return best

    def nosing(self):
        return sorted(self.w(ly, lz) for ly, lz in self.p["nosing"])

    def nosing_z(self, y):
        pts = self.nosing()
        if y < pts[0][0] - 0.5 or y > pts[-1][0] + 0.5:
            return None
        for (ya, za), (yb, zb) in zip(pts, pts[1:]):
            if ya - 0.5 <= y <= yb + 0.5:
                return za if abs(yb - ya) < 1e-3 else za + (zb - za) * (y - ya) / (yb - ya)
        return pts[-1][1]

    def floor(self):
        return self.w(0.0, self.p["floor_z"])[1]

    def yrange(self):
        tr = self.treads()
        return tr[0][0], tr[-1][1]


# ---------------------------------------------------------------- 트레이스
def _trace_col(y, x, z_from=2000.0, z_to=-500.0):
    """위에서 아래로: 걷기 COL(COL_ 이고 Prop/Bound/게이트가 아닌 것) 중 첫 번째 윗면."""
    world = _world()
    ignore = []
    for _ in range(12):
        h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, z_from), unreal.Vector(x, y, z_to),
                                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignore, unreal.DrawDebugTrace.NONE, True)
        if not h:
            return None, None
        t = h.to_tuple()
        act = t[9]
        lab = act.get_actor_label() if act else ""
        if lab.startswith("COL_") and not lab.startswith(("COL_Prop_", "COL_Bound", "COL_HackGate")):
            return t[4].z, lab
        ignore.append(act)
    return None, None


def _sample_xs(art, col_x):
    x0, x1 = max(art.x0, col_x[0]), min(art.x1, col_x[1])
    xs = [(x0 + x1) / 2]
    if x1 - x0 > 200:
        xs = [x0 + 40, (x0 + x1) / 2, x1 - 40]
    return [round(x) for x in xs]


def _trace_stair(s, lab):
    art = Art(lab[s["art"]])
    colname = s.get("ramp") or s.get("flat")
    cb = _bounds(lab[colname])
    y0, y1 = art.yrange()
    xs = _sample_xs(art, (cb[0], cb[1]))
    z_from = _bounds(art.a)[5] + 100.0      # 위층 천장 COL 에 먼저 맞지 않게 계단 꼭대기 1m 위에서
    rows = []
    n = max(6, int((y1 - y0) / 25))
    for i in range(n + 1):
        y = y0 + 3 + (y1 - y0 - 6) * i / n
        nz, tz = art.nosing_z(y), art.tread_z(y)
        for x in xs:
            cz, cl = _trace_col(y, x, z_from)
            rows.append({"x": x, "y": round(y, 1), "col_z": _r(cz) if cz is not None else None, "col": cl,
                         "nosing_z": _r(nz), "tread_z": _r(tz), "dz_nosing": _r(cz - nz) if cz is not None and nz is not None else None})
    dz = [abs(r["dz_nosing"]) for r in rows if r["dz_nosing"] is not None]
    return {"id": s["id"], "art": s["art"], "mesh": art.mesh, "xs": xs, "samples": len(rows), "max_abs_dz": _r(max(dz) if dz else None),
            "mean_abs_dz": _r(sum(dz) / len(dz) if dz else None), "over_tol": sum(1 for d in dz if d > TOL_Z), "rows": rows}


def _trace_flat(s, lab):
    """윗면이 평평한 아트(교체한 구덩이): 아트 윗면 ↔ COL 트레이스."""
    a = lab[s["flat_art"]]
    ab, cb = _bounds(a), _bounds(lab[s["flat"]])
    rows = []
    for i in range(7):
        y = ab[2] + 3 + (ab[3] - ab[2] - 6) * i / 6
        for x in (-215, -43, 130):
            cz, cl = _trace_col(y, x, ab[5] + 100.0)
            rows.append({"x": x, "y": round(y, 1), "col_z": _r(cz) if cz is not None else None, "col": cl, "nosing_z": _r(ab[5]), "tread_z": _r(ab[5]),
                         "dz_nosing": _r(cz - ab[5]) if cz is not None else None})
    dz = [abs(r["dz_nosing"]) for r in rows if r["dz_nosing"] is not None]
    return {"id": s["id"], "art": s["flat_art"], "mesh": a.static_mesh_component.static_mesh.get_name(), "samples": len(rows),
            "max_abs_dz": _r(max(dz)), "mean_abs_dz": _r(sum(dz) / len(dz)), "over_tol": sum(1 for d in dz if d > TOL_Z), "rows": rows,
            "art_bounds": _r(ab), "col": s["flat"], "col_bounds": _r(cb), "fix": True, "note": s["note"]}


def _compare_bounds(s, lab):
    """비교값: 아트 계단의 시작·끝 Y, 아래·위 Z, 폭(X) ↔ 짝 콜라이더(경사 + 같은 Y 구간에서 가장 높은 평평한 COL)."""
    art = Art(lab[s["art"]])
    ab = _bounds(art.a)
    colname = s.get("ramp") or s.get("flat")
    cb = _bounds(lab[colname])
    out = {"art_bounds": _r(ab), "col": colname, "col_bounds": _r(cb)}
    # 계단 코 선의 시작/끝 (화면에 보이는 걷는 면)과 콜라이더 윗면의 같은 점 높이
    pts = art.nosing()
    out["art_nosing"] = [[_r(y), _r(z)] for y, z in pts]
    return out


def audit(phase):
    lab = _by_label()
    res = {"phase": phase, "level": LEVEL, "tol_z": TOL_Z, "tol_y": TOL_Y, "stairs": []}
    for s in STAIRS:
        if s.get("flat_art") and s["art"] not in lab and s["flat_art"] in lab:
            res["stairs"].append(_trace_flat(s, lab))
            continue
        if s["art"] is None:
            rows = []
            for art_l, col_l in s["pairs"]:
                ab, cb = _bounds(lab[art_l]), _bounds(lab[col_l])
                ym = (cb[2] + cb[3]) / 2
                cz, cl = _trace_col(ym, -60.0, ab[5] + 100.0)
                rows.append({"art": art_l, "col": col_l, "art_top": _r(ab[5]), "col_top": _r(cb[5]), "art_y": _r(ab[2:4]), "col_y": _r(cb[2:4]),
                             "trace_y": _r(ym), "trace_z": _r(cz), "dz": _r(cz - ab[5]) if cz is not None else None})
            res["stairs"].append({"id": s["id"], "note": s["note"], "pairs": rows,
                                  "max_abs_dz": _r(max(abs(r["dz"]) for r in rows if r["dz"] is not None))})
            continue
        r = _trace_stair(s, lab)
        r.update(_compare_bounds(s, lab))
        r["fix"] = s.get("fix", False)
        r["note"] = s.get("note", "")
        res["stairs"].append(r)
    old = {}
    if os.path.exists(AUDIT):
        old = json.load(open(AUDIT, encoding="utf-8"))
    old[phase] = res
    json.dump(old, open(AUDIT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    for st in res["stairs"]:
        print("%-13s max|dz| %6s  mean %6s  over_tol %s" % (st["id"], st.get("max_abs_dz"), st.get("mean_abs_dz", "-"), st.get("over_tol", "-")))
    return res


# ---------------------------------------------------------------- 수정
def _ramp_xf(x0, x1, ya, za, yb, zb, thick):
    """윗면이 (ya,za)→(yb,zb) 인 큐브 경사(build_map2.ramp 와 같은 방식)."""
    if yb < ya:
        ya, za, yb, zb = yb, zb, ya, za
    dy, dz = yb - ya, zb - za
    L = math.hypot(dy, dz)
    ang = math.degrees(math.atan2(dz, dy))
    ny, nz = -dz / L, dy / L
    return {"loc": [(x0 + x1) / 2, (ya + yb) / 2 - ny * thick / 2, (za + zb) / 2 - nz * thick / 2], "rot": [ang, 90.0, 0.0],
            "scale": [L / 100, (x1 - x0) / 100, thick / 100], "angle": abs(ang)}


def _box_xf(x0, x1, y0, y1, z0, z1):
    return {"loc": [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], "rot": [0.0, 0.0, 0.0], "scale": [(x1 - x0) / 100, (y1 - y0) / 100, (z1 - z0) / 100]}


def _clone(src, label, folder):
    a = _eas().duplicate_actor(src, _world(), unreal.Vector(0, 0, 0))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.tags = [TAG, TAG_ADDED]
    return a


def plan_stair(s, lab):
    """수정 조각 목록: (kind, label, xf, info). kind = modify(기존 액터) / add(복제)."""
    art = Art(lab[s["art"]])
    base = lab[s.get("ramp") or s.get("flat")]
    cb = _bounds(base)
    x0, x1 = cb[0], cb[1]
    fl = art.floor()
    out = []
    for piece in art.p["walk"]:
        if piece[0] == "ramp":
            (ya, za), (yb, zb) = art.w(*piece[1]), art.w(*piece[2])
            xf = _ramp_xf(x0, x1, ya, za, yb, zb, s.get("ramp_t", RAMP_T))
            name = piece[3]
            if name == "RAMP":
                out.append(("modify", s["ramp"], xf, {"from": _r([ya, za]), "to": _r([yb, zb]), "angle": _r(xf["angle"])}))
            else:
                out.append(("add", "COL_%s_%s" % (s["id"], name), xf, {"from": _r([ya, za]), "to": _r([yb, zb]), "angle": _r(xf["angle"])}))
        else:
            _, ly0, ly1, lz, name = piece
            (ya, zt), (yb, _) = art.w(ly0, lz), art.w(ly1, lz)
            ya, yb = min(ya, yb), max(ya, yb)
            zb = fl if s.get("under") is None else max(fl, zt - s["under"])
            if name == "FirstStep":
                zb = fl
            xf = _box_xf(x0, x1, ya, yb, zb, zt)
            if name == "FLAT":
                out.append(("modify", s["flat"], xf, {"box": _r([x0, x1, ya, yb, zb, zt])}))
            else:
                out.append(("add", "COL_%s_%s" % (s["id"], name), xf, {"box": _r([x0, x1, ya, yb, zb, zt])}))
    return out, base


def plan_gate(lab):
    s = [x for x in STAIRS if x["id"] == GATE["stair"]][0]
    art = Art(lab[s["art"]])
    floor_y = _bounds(lab[GATE["col"]])
    gy = (floor_y[2] + floor_y[3]) / 2
    # 게이트 바로 앞(바위 A 쪽) 아트 바닥 높이
    fz = art.nosing_z(min(gy, art.yrange()[1] - 1)) or art.floor()
    fz = max(art.floor(), fz)
    out = []
    for a in _eas().get_all_level_actors():
        l = a.get_actor_label()
        if not l.startswith(GATE["bar_prefix"]):
            continue
        b = _bounds(a)
        if l.startswith(GATE["rail_prefix"]):
            if l != GATE["rail_bottom_label"]:
                continue
            xf = _xf(a)
            xf["loc"][2] = fz + 4.0          # add_map2_hacking.laser_gate 와 같이 막대 아래 끝 + 4
            out.append(("modify", l, xf, {"rail_z": _r(xf["loc"][2])}))
        else:
            top = b[5]
            xf = _xf(a)
            xf["loc"][2] = (fz + top) / 2
            xf["scale"][2] = (top - fz) / 100.0
            out.append(("modify", l, xf, {"bar": _r([fz, top])}))
    c = lab[GATE["col"]]
    b = _bounds(c)
    xf = _xf(c)
    xf["loc"][2] = (GATE["col_bottom"] + b[5]) / 2
    xf["scale"][2] = (b[5] - GATE["col_bottom"]) / 100.0
    out.append(("modify", GATE["col"], xf, {"col_z": _r([GATE["col_bottom"], b[5]])}))
    return out, fz


def _validate(lab):
    """수정 후: 고친 계단마다 트레이스 |dz| ≤ 15(계단 코 선 기준), 경사 ≤ 44.8°, 캡슐(오빠/여동생)이 계단 위에 설 수 있는지."""
    ok = True
    world = _world()
    for s in STAIRS:
        if not s.get("fix"):
            continue
        r = _trace_stair(s, lab)
        bad = [row for row in r["rows"] if row["dz_nosing"] is None or abs(row["dz_nosing"]) > TOL_Z]
        print("  verify %-12s max|dz| %s over_tol %d" % (s["id"], r["max_abs_dz"], len(bad)))
        if bad:
            ok = False
            print("    FAIL rows:", bad[:4])
        # 캡슐 서기: 계단 코 선 위 3cm 에서 겹침 없음
        art = Art(lab[s["art"]])
        y0, y1 = art.yrange()
        for i in range(1, 6):
            y = y0 + (y1 - y0) * i / 6
            z = art.nosing_z(y)
            for hh in (88.0, 80.0):
                x = (max(art.x0, -240) + min(art.x1, 150)) / 2
                hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(x, y, z + hh + 15), 34.0, hh,
                                                                   [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], unreal.StaticMeshActor, [])
                hits = [h.get_actor_label() for h in (hits or []) if h.get_actor_label().startswith("COL_") and not h.get_actor_label().startswith("COL_HackGate")]
                if hits:
                    print("    capsule blocked", s["id"], round(y), hh, hits[:3])
                    ok = False
    return ok


def apply():
    if _world().get_outermost().get_name() != LEVEL:
        print("REFUSED: current level is", _world().get_outermost().get_name()); return False
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() + unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    if dirty:
        print("REFUSED: dirty packages (unsaved edits):", dirty); return False
    actors = _eas().get_all_level_actors()
    if any(TAG in _tags(a) for a in actors) or os.path.exists(BACKUP):
        print("REFUSED: already applied (tag %s or %s exists). Use STAIR_MODE='undo' first." % (TAG, BACKUP)); return False
    lab = _by_label()
    backup = {"level": LEVEL, "tag": TAG, "modified": [], "added": [], "gate_floor_z": None}
    steps = []
    for s in STAIRS:
        if not s.get("fix"):
            continue
        p, base = plan_stair(s, lab)
        steps += [(k, l, xf, info, base, s["id"]) for k, l, xf, info in p]
    gp, gfz = plan_gate(lab)
    backup["gate_floor_z"] = _r(gfz)
    steps += [(k, l, xf, info, None, "Gate_S5Pit") for k, l, xf, info in gp]
    # 백업 먼저(실패해도 되돌릴 수 있게)
    for k, l, xf, info, base, sid in steps:
        if k == "modify":
            a = lab[l]
            backup["modified"].append({"label": l, "stair": sid, "before": _xf(a), "before_bounds": _r(_bounds(a)), "tags": _tags(a), "after": {kk: _r(v, 3) for kk, v in xf.items() if kk != "angle"}, "info": info})
        else:
            backup["added"].append({"label": l, "stair": sid, "xf": {kk: _r(v, 3) for kk, v in xf.items() if kk != "angle"}, "info": info})
    json.dump(backup, open(BACKUP, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    with unreal.ScopedEditorTransaction("Map2 stair collision fix") as _t:
        for k, l, xf, info, base, sid in steps:
            if k == "modify":
                a = lab[l]
                _set_xf(a, xf)
                if TAG not in _tags(a):
                    a.tags = list(a.tags) + [TAG]
            else:
                a = _clone(base, l, "Collision/Stairs")
                _set_xf(a, xf)
            print("  %-6s %-38s %s" % (k, l, info))
    lab = _by_label()
    ok = _validate(lab)
    if not ok:
        print("VALIDATION FAILED — not saved. Run STAIR_MODE='undo' to restore.")
        return False
    audit("after")
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    print("saved:", les.save_current_level())
    return True


# ---------------------------------------------------------------- 2단계(2026-11-07 사용자 결정): 구덩이 아트 교체 + 단말기 내리기
PIT = {"old": "SM_Slums_Wall_02o2", "new": "SM_Slums_Wall_02k_S5RockPit", "mesh_src": "SM_Slums_Wall_02k11", "col": "COL_S5_RockPit",
       "x": (-255.0, 210.0)}          # 원래 구덩이 아트 X 범위. 메시는 옆 S5 계단 받침과 같은 SM_Slums_Wall_02k(윗면 평평, yaw 90)
TERMINAL = "HackTerminal_M2_Memory"


def _mesh_record(a):
    c = a.static_mesh_component
    return {"label": a.get_actor_label(), "mesh": c.static_mesh.get_path_name(), "materials": [m.get_path_name() if m else None for m in c.get_materials()],
            "folder": str(a.get_folder_path()), "tags": _tags(a), "xf": _xf(a), "bounds": _r(_bounds(a))}


def _spawn_record(r):
    a = _eas().spawn_actor_from_object(unreal.load_asset(r["mesh"]), unreal.Vector(*r["xf"]["loc"]))
    _set_xf(a, r["xf"])
    c = a.static_mesh_component
    c.set_collision_profile_name("NoCollision")
    c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    for i, m in enumerate(r["materials"]):
        if m:
            c.set_material(i, unreal.load_asset(m))
    a.set_folder_path(r["folder"])
    a.set_actor_label(r["label"])
    a.tags = r["tags"]
    return a


def _fit_yaw90(a, box):
    """yaw 90 메시를 월드 박스에 맞춘다(로컬 X → 월드 Y, 로컬 Y → 월드 X)."""
    b = _bounds(a); s = a.get_actor_scale3d()
    rx = (box[1] - box[0]) / max(b[1] - b[0], 1e-3)
    ry = (box[3] - box[2]) / max(b[3] - b[2], 1e-3)
    rz = (box[5] - box[4]) / max(b[5] - b[4], 1e-3)
    a.set_actor_scale3d(unreal.Vector(s.x * ry, s.y * rx, s.z * rz))
    b = _bounds(a); l = a.get_actor_location()
    a.set_actor_location(unreal.Vector(l.x + (box[0] + box[1] - b[0] - b[1]) / 2, l.y + (box[2] + box[3] - b[2] - b[3]) / 2,
                                       l.z + (box[4] + box[5] - b[4] - b[5]) / 2), False, False)


def apply2():
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() + unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    if dirty:
        print("REFUSED: dirty packages (unsaved edits):", dirty); return False
    if not os.path.exists(BACKUP):
        print("REFUSED: stage 1 (apply) not found"); return False
    b = json.load(open(BACKUP, encoding="utf-8"))
    if b.get("stage2"):
        print("REFUSED: stage 2 already applied"); return False
    lab = _by_label()
    old, col, term = lab[PIT["old"]], lab[PIT["col"]], lab[TERMINAL]
    cb = _bounds(col)
    box = [PIT["x"][0], PIT["x"][1], cb[2], cb[3], 0.0, cb[5]]          # 구덩이 발자국(Y 12750~12900), 윗면 = COL 77
    # 단말기 아래 COL(바위 A 경사) 높이 — add_map2_hacking.floor_at 과 같은 트레이스(COL_Prop_·단말기 제외)
    ignore = [a for a in _eas().get_all_level_actors() if a.get_actor_label().startswith("COL_Prop_") or isinstance(a, unreal.HackTerminal)]
    tl = term.get_actor_location()
    h = unreal.SystemLibrary.line_trace_single(_world(), unreal.Vector(tl.x, tl.y, 3000), unreal.Vector(tl.x, tl.y, -800),
                                               unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignore, unreal.DrawDebugTrace.NONE, True)
    t = h.to_tuple()
    tz, under = t[4].z, t[9].get_actor_label()
    unlock_before = [x.get_actor_label() for x in term.get_editor_property("unlock_actors")]
    b["stage2"] = {"deleted": [_mesh_record(old)], "added_art": PIT["new"], "pit_box": _r(box),
                   "terminal": {"floor": under, "z_before": _r(tl.z), "z_after": _r(tz), "unlock": unlock_before}}
    b["modified"].append({"label": TERMINAL, "stair": "S5_RockA", "before": _xf(term), "before_bounds": _r(_bounds(term)), "tags": _tags(term),
                          "info": {"floor": under, "z": [_r(tl.z), _r(tz)]}})
    json.dump(b, open(BACKUP, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    with unreal.ScopedEditorTransaction("Map2 stair fix stage 2") as _t:
        src = lab[PIT["mesh_src"]]
        n = _eas().duplicate_actor(src, _world(), unreal.Vector(0, 0, 0))
        n.set_actor_label(PIT["new"])
        n.set_folder_path(str(old.get_folder_path()))
        n.tags = ["GEN_Map2", TAG, TAG_ADDED]
        n.set_actor_rotation(unreal.Rotator(pitch=0.0, yaw=90.0, roll=0.0), False)
        _fit_yaw90(n, box)
        _eas().destroy_actor(old)
        term.set_actor_location(unreal.Vector(tl.x, tl.y, tz), False, False)
        term.set_editor_property("terminal_name", term.get_editor_property("terminal_name"))   # OnConstruction → 상호작용 박스 갱신
        if TAG not in _tags(term):
            term.tags = list(term.tags) + [TAG]
    print("pit art ->", PIT["new"], _r(_bounds(n)), "| terminal z", _r(tl.z), "->", _r(tz), "on", under)
    unlock_after = [x.get_actor_label() for x in term.get_editor_property("unlock_actors")]
    ok = unlock_after == unlock_before
    print("unlock links kept:", ok, len(unlock_after))
    if not ok:
        print("FAILED - not saved"); return False
    audit("after")
    print("saved:", unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level())
    return True


def undo():
    if not os.path.exists(BACKUP):
        print("no backup", BACKUP); return False
    b = json.load(open(BACKUP, encoding="utf-8"))
    lab = _by_label()
    n_del = 0
    for a in _eas().get_all_level_actors():
        if TAG_ADDED in _tags(a):
            _eas().destroy_actor(a); n_del += 1
    for r in b.get("stage2", {}).get("deleted", []):
        _spawn_record(r)
        print("  recreated", r["label"])
    for m in b["modified"]:
        a = lab.get(m["label"])
        if not a:
            print("  missing", m["label"]); continue
        _set_xf(a, m["before"])
        a.tags = m["tags"]
    os.remove(BACKUP)
    print("undo: deleted %d added, restored %d. Level is dirty — save to keep." % (n_del, len(b["modified"])))
    return True


_mode = globals().get("STAIR_MODE", "audit")
if _mode == "audit":
    audit("before")
elif _mode == "verify":
    audit("after")
elif _mode == "apply":
    apply()
elif _mode == "apply2":
    apply2()
elif _mode == "undo":
    undo()
elif _mode == "plan":
    _lab = _by_label()
    for _s in STAIRS:
        if _s.get("fix"):
            for _p in plan_stair(_s, _lab)[0]:
                print(_s["id"], _p[0], _p[1], _p[3])
    for _p in plan_gate(_lab)[0]:
        print("gate", _p[0], _p[1], _p[3])
