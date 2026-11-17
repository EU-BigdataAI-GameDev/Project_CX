"""
Map2 S5 마지막 단말기 구역 — 사용자가 손으로 옮긴 아트에 걷기 COL 다시 맞추기 (2026-11-07).

사용자 배치(기준, 절대 옮기지 않음):
  SM_Slums_Wall_02k12        평평한 받침(윗면 ≈226.5, Y 12347~12722)
  SM_Slums_Wall_02o          계단 벽(받침 위로 올라감: 계단참 301 → 계단 → 받침 윗면 아래로 사라짐)
  SM_Slums_Wall_02o3         계단 벽(계단참 220 → 계단 → 계단참 → 계단 → 끝 ≈38, Y 12720~13120)
  SM_Slums_Wall_02k_S5RockPit 평평한 블록(윗면 37, Y 13112~13337)
보이는 윗면 = 각 Y 에서 이 아트들 윗면의 최댓값(계단은 계단 코 선). fix_map2_stairs.py 의 Art 단면 모델을 그대로 쓴다.

하는 일: 우리 COL(태그 GEN_Map2StairFix)만 옮기고, 필요 없는 것(COL_S5_RockA_Ramp2)은 지우고, 구덩이 블록 COL 을 새로 만든다.
  - 맵을 저장하지 않는다(사용자가 저장). 다른 맵을 열지 않는다. 한 번의 에디터 트랜잭션(Ctrl+Z 가능).
  - 수정 전 변환은 Docs/LevelManifests/Map2_s5_relayout_backup.json 에 남긴다.
실행: globals()["S5_MODE"] = "plan" | "apply" | "verify" ; exec(open(<이 파일>).read())
"""
import unreal, json, os
globals()["STAIR_MODE"] = "none"
exec(open(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Tools/LevelBuild/fix_map2_stairs.py"), encoding="utf-8").read())

S5_BACKUP = MAN_DIR + "Map2_s5_relayout_backup.json"
X0, X1 = -275.0, 170.0
ART = {"base": "SM_Slums_Wall_02k12", "upper": "SM_Slums_Wall_02o", "lower": "SM_Slums_Wall_02o3", "pit": "SM_Slums_Wall_02k_S5RockPit"}


def _layout(lab):
    base, pit = _bounds(lab[ART["base"]]), _bounds(lab[ART["pit"]])
    up, lo = Art(lab[ART["upper"]]), Art(lab[ART["lower"]])
    return base, pit, up, lo


def visible_top(y, lab):
    base, pit, up, lo = _layout(lab)
    zs = []
    if base[2] <= y <= base[3]:
        zs.append(base[5])
    if pit[2] <= y <= pit[3]:
        zs.append(pit[5])
    for a in (up, lo):
        z = a.nosing_z(y)
        if z is not None:
            zs.append(z)
    return max(zs) if zs else None


def plan(lab):
    base, pit, up, lo = _layout(lab)
    bt = base[5]
    un = up.nosing()            # [(12350,301),(12425,301),(12614,148),(12670,148),(12750,97)]
    (ya, za), (yb, zb) = un[1], un[2]
    y_cut = ya + (yb - ya) * (za - bt) / (za - zb)      # 위 계단 코 선이 받침 윗면과 만나는 Y
    ln = lo.nosing()            # 계단참 → 계단 → 계단참 → 계단
    steps = [
        ("COL_S5_RockA", _box_xf(X0, X1, un[0][0], un[1][0], 0.0, un[0][1]), "upper landing"),
        ("COL_S5_RockA_Ramp1", _ramp_xf(X0, X1, ya, za, y_cut, bt, 30.0), "upper stair -> base top"),
        ("COL_S5_RockA_MidLanding", _box_xf(X0, X1, un[1][0], base[3], 0.0, bt), "base top (flat)"),
        ("COL_S5_RockB", _box_xf(X0, X1, base[3], ln[1][0], 0.0, ln[0][1]), "lower landing (gate)"),
        ("COL_S5_RockB_Ramp1", _ramp_xf(X0, X1, ln[1][0], ln[1][1], ln[2][0], ln[2][1], 30.0), "lower stair 1"),
        ("COL_S5_RockB_MidLanding", _box_xf(X0, X1, ln[2][0] - 15.0, ln[3][0], 0.0, ln[3][1]), "lower mid landing"),
        ("COL_S5_RockB_Ramp2", _ramp_xf(X0, X1, ln[3][0], ln[3][1], ln[4][0], ln[4][1], 30.0), "lower stair 2"),
        ("+COL_S5_PitBlock", _box_xf(X0, X1, ln[4][0], pit[3], 0.0, pit[5]), "pit block (flat)"),
        ("-COL_S5_RockA_Ramp2", None, "stale (old lower stair at 12654~12750) - inside base now"),
    ]
    return steps, y_cut


def apply_s5():
    lab = _by_label()
    if os.path.exists(S5_BACKUP):
        print("REFUSED: already applied (%s exists)" % S5_BACKUP); return False
    steps, y_cut = plan(lab)
    bk = {"note": "S5 relayout to user's hand-moved art (not saved by script)", "art_bounds": {k: _r(_bounds(lab[v])) for k, v in ART.items()},
          "changed": [], "deleted": [], "added": []}
    for lbl, xf, info in steps:
        if lbl.startswith("+"):
            bk["added"].append({"label": lbl[1:], "xf": {k: _r(v, 3) for k, v in xf.items() if k != "angle"}, "info": info})
        elif lbl.startswith("-"):
            a = lab[lbl[1:]]
            bk["deleted"].append({"label": lbl[1:], "before": _xf(a), "before_bounds": _r(_bounds(a)), "tags": _tags(a), "folder": str(a.get_folder_path())})
        else:
            a = lab[lbl]
            bk["changed"].append({"label": lbl, "before": _xf(a), "before_bounds": _r(_bounds(a)), "after": {k: _r(v, 3) for k, v in xf.items() if k != "angle"},
                                  "angle": _r(xf.get("angle", 0.0)), "info": info})
    json.dump(bk, open(S5_BACKUP, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    with unreal.ScopedEditorTransaction("Map2 S5 collision to user art") as _t:
        for lbl, xf, info in steps:
            if lbl.startswith("+"):
                a = _clone(lab["COL_S5_RockB"], lbl[1:], "Collision/Stairs")
                _set_xf(a, xf)
            elif lbl.startswith("-"):
                _eas().destroy_actor(lab[lbl[1:]])
            else:
                _set_xf(lab[lbl], xf)
                if TAG not in _tags(lab[lbl]):
                    lab[lbl].tags = list(lab[lbl].tags) + [TAG]
            print("  %-26s %s %s" % (lbl, info, _r(xf.get("angle", 0.0)) if xf else ""))
    print("y_cut", _r(y_cut), "- NOT saved (user saves)")
    return verify_s5()


def verify_s5():
    lab = _by_label()
    base, pit, up, lo = _layout(lab)
    y0, y1 = up.yrange()[0], pit[3]
    rows, worst = [], 0.0
    y = y0 + 3
    while y < y1 - 3:
        vz = visible_top(y, lab)
        for x in (-215.0, -43.0, 130.0):
            cz, cl = _trace_col(y, x, 500.0)
            d = None if cz is None or vz is None else cz - vz
            rows.append({"x": x, "y": _r(y), "art_z": _r(vz), "col_z": _r(cz), "col": cl, "dz": _r(d)})
            if d is not None:
                worst = max(worst, abs(d))
        y += 25.0
    over = [r for r in rows if r["dz"] is None or abs(r["dz"]) > TOL_Z]
    print("S5 relayout verify: samples %d  max|dz| %.1f  over_tol %d" % (len(rows), worst, len(over)))
    for r in over[:8]:
        print("   over", r)
    # 캡슐 서기
    world = _world()
    bad = []
    for yy in range(int(y0) + 20, int(y1) - 20, 40):
        z = visible_top(yy, lab)
        for hh in (88.0, 80.0):
            hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(-43, yy, z + hh + 15), 34.0, hh,
                                                               [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], unreal.StaticMeshActor, [])
            hits = [h.get_actor_label() for h in (hits or []) if h.get_actor_label().startswith("COL_") and not h.get_actor_label().startswith("COL_HackGate")]
            if hits:
                bad.append((yy, hh, hits[:2]))
    print("capsule blocked:", bad[:6])
    out = {"samples": len(rows), "max_abs_dz": _r(worst), "over_tol": len(over), "capsule_blocked": bad, "rows": rows}
    old = json.load(open(AUDIT, encoding="utf-8")) if os.path.exists(AUDIT) else {}
    old["s5_relayout"] = out
    json.dump(old, open(AUDIT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    return out


_m = globals().get("S5_MODE", "plan")
if _m == "plan":
    _lab = _by_label()
    _st, _yc = plan(_lab)
    print("y_cut", _r(_yc))
    for _l, _x, _i in _st:
        print(_l, _i, (_r(_x["loc"]), _r(_x["rot"]), _r(_x["scale"], 3), _r(_x.get("angle", 0.0))) if _x else "")
elif _m == "apply":
    apply_s5()
elif _m == "verify":
    verify_s5()
