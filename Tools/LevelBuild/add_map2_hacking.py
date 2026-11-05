"""
Map2_SoulCity2 에 단말기 퍼즐 3종(.claude/Plans/해킹 시스템 기획.md 0-1장 — 비밀번호 / 주파수 동조 / 메모리 패턴)을 추가한다.

- 사람이 손본 레벨이므로 **추가만** 한다: 태그 GEN_Map2Hack 이 붙은 액터만 지우고 다시 만든다(다른 액터는 이동/삭제하지 않음).
- 폴더 Gameplay/Hacking. 바닥 높이는 COL_ 박스에 라인트레이스(COL_Prop_* 와 단말기는 무시)로 구한다.
- 실행 전 레벨이 이미 dirty(다른 사람의 저장 안 한 수정)면 배치는 하되 **저장하지 않고** 알린다.

배치(맵2_레벨디자인.md 4장 구간 기준, 진행 +Y, 카메라 -X, 레인 X -120~150):
  1) 비밀번호  SHUTTER_CTRL-5  — S2 합류 마당(Y 5920, 뒤쪽 X 110). 게이트: S3 공동주택 입구 문(Y 5995, 바닥 0~260 — 문 위 벽은 Z 250부터).
     숫자 단서 4개(오빠 화면 전용): 오빠 동선 S1 판자 발판 → S2 지붕 R1 → R2 → R4, 레인 뒤 X 155, 발판 위 230cm, yaw 180.
  2) 주파수 동조 MARKET_RELAY  — S4 옥상 입구(Y 9590, 뒤쪽 X 110, Z 728). 게이트: S4 낮은 마당 직전(Y 10475, Z 728~1050).
     게이트 앞 구간(Y 9420~10455)에는 적이 없다 — 거미 S4_01(Y 11224)은 게이트 뒤.
  3) 메모리 패턴 CAVE_LIFT_ECHO — S5 바위 턱 A(Y 12505, 뒤쪽 X 110, Z 200). 게이트: 바위 A 끝/구덩이 시작(Y 12750, Z 60~540).
     게이트 앞 바위 A·계단 위는 단말기에서 9m(MemoryRelayRange) 안 → 오빠가 함께 서서 패턴을 중계할 수 있다.
게이트 = 투명 COL 박스(레인 전체 X -125~155, BlockAll·Camera 무시·게임 중 숨김) + 붉은 네온 막대(add_map1_hacking.py 와 같은 문법).
단말기는 게이트 **앞**(진행 방향 뒤쪽)에 있어 소프트락이 없다. 게이트는 단말기 해킹 성공 시 UnlockActors 로 숨김·충돌 해제.

실행(에디터 원격 실행): 1회차 — 현재 레벨이 Map2가 아니면 불러오고 끝(로드는 자기 호출에서만). 2회차 — 배치 + 검사 + 저장.
"""
import unreal, json

LEVEL = "/Game/Project_CX/Maps/Map2_SoulCity2"
TAG = "GEN_Map2Hack"
FOLDER = "Gameplay/Hacking"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TERMINAL_MESH = "/Game/SoulCity/Environment/Meshes/Building_Slum/SM_Slums_Vent_02b"   # Map1·복도와 같은 단말기 메시
NEON_RED = "/Game/Project_CX/Environment/Materials/MI_Neon_Red"
LANE_F, LANE_B = -120.0, 150.0
PATH_X = -60.0          # 레인 앞쪽 통로(검사용)
GATE_X0, GATE_X1 = -125.0, 155.0   # 앞/뒤 경계벽(COL_Bound_Front/Back)과 살짝 겹치게

# 2026-11-04: Map2 는 scale_map2_vertical.py 로 세로 1.1배가 됐다(태그 GEN_Map2VScale 표식 액터). 아래 게이트 Z 는 1.1배 전 기준이고,
# 표식이 있으면 place() 가 VZ = 1.1 을 곱한다(단말기·단서 바닥은 원래도 COL 트레이스로 구하므로 그대로 맞는다. 단서 높이 230 은 유지).
MAP2_VSCALE_TAG = "GEN_Map2VScale"
TERMINALS = [
    # label, terminal_name, puzzle, (x, y), gate(label, y, z0, z1, bar_z0)
    ("HackTerminal_M2_Passcode", "SHUTTER_CTRL-5", "PASSCODE", (110.0, 5920.0), ("HackGate_M2_S3Door", 5995.0, 0.0, 262.0, 0.0)),
    ("HackTerminal_M2_FreqLock", "MARKET_RELAY", "FREQUENCY_LOCK", (110.0, 9590.0), ("HackGate_M2_S4Yard", 10475.0, 728.0, 1050.0, 728.0)),
    ("HackTerminal_M2_Memory", "CAVE_LIFT_ECHO", "MEMORY_PATTERN", (110.0, 12505.0), ("HackGate_M2_S5Pit", 12750.0, 60.0, 540.0, 200.0)),
]
# 비밀번호 단서: label, y, digit_index(자리 순서를 섞어 둠 — 오빠 동선 순서로 #2 → #4 → #1 → #3)
CLUES = [("HackClue_M2_S1_Canal", 1300.0, 1), ("HackClue_M2_S2_R1", 3250.0, 3), ("HackClue_M2_S2_R2", 4250.0, 0), ("HackClue_M2_S2_R4", 5180.0, 2)]
CLUE_X, CLUE_H = 155.0, 230.0
# 2026-11-04: widen_lane_front.py 로 레인 앞(카메라 쪽)이 넓어졌으면(표식 액터 태그 'LaneFrontDelta=<cm>', Map2 1.1배 = 27 → 2026-11-06 1.5배 = 135)
# 게이트 COL 앞 끝·레일·네온 막대를 그만큼 앞으로 늘린다 → 재실행해도 넓어진 레인 전체를 막는다.
LANE_FRONT_TAG = "LaneFrontDelta="


def lane_front_delta(eas):
    for a in eas.get_all_level_actors():
        for t in a.tags:
            if str(t).startswith(LANE_FRONT_TAG):
                return float(str(t)[len(LANE_FRONT_TAG):])
    return 0.0


def place():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    was_dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    # 같은 세션에서 이 스크립트를 다시 돌려 생긴 dirty(자기 액터만 바뀐 상태)는 호출자가 M2_HACK_RERUN = True 로 알려 준다
    if was_dirty and globals().get("M2_HACK_RERUN"):
        print("dirty before run (declared as this script's own previous run):", was_dirty)
        was_dirty = []

    removed = 0
    for a in eas.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            removed += 1

    TerminalClass = unreal.load_class(None, "/Script/Project_CX.HackTerminal")
    ClueClass = unreal.load_class(None, "/Script/Project_CX.HackCodeClue")
    ignore = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("COL_Prop_") or a.get_class() == TerminalClass]

    def floor_at(x, y, z_from=3000.0):
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, z_from), unreal.Vector(x, y, -800),
                                                     unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignore, unreal.DrawDebugTrace.NONE, True)
        if not hit:
            raise RuntimeError("no floor at %s,%s" % (x, y))
        t = hit.to_tuple()
        return t[4].z, t[9].get_actor_label()

    manifest = []

    def finish(a, folder, label, kind, extra=None):
        a.set_folder_path(folder)
        a.set_actor_label(label)
        a.tags = [TAG]
        l = a.get_actor_location()
        e = {"label": label, "kind": kind, "loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)]}
        if extra:
            e.update(extra)
        manifest.append(e)
        return a

    def neon_cube(loc, scale, mi):
        c = eas.spawn_actor_from_object(unreal.load_asset(CUBE), loc)
        c.set_actor_scale3d(scale)
        sm = c.static_mesh_component
        sm.set_material(0, mi)
        sm.set_collision_profile_name("NoCollision")
        sm.set_editor_property("cast_shadow", False)
        return c

    ld = lane_front_delta(eas)
    gx0, lf = GATE_X0 - ld, LANE_F - ld
    bars_x = [float(x) for x in range(-100, 141, 40)]
    while bars_x[0] - 40 >= lf + 5:          # 넓어진 앞쪽 폭에도 같은 간격(-140)
        bars_x.insert(0, bars_x[0] - 40)
    print("lane front delta:", ld, "gate X", gx0, "..", GATE_X1, "bars", bars_x)

    def laser_gate(label, y, z0, z1, bar_z0):
        folder = f"{FOLDER}/Gate_{label.split('_')[-1]}"
        parts = []
        col = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((gx0 + GATE_X1) / 2, y, (z0 + z1) / 2))
        col.set_actor_scale3d(unreal.Vector((GATE_X1 - gx0) / 100, 0.3, (z1 - z0) / 100))
        c = col.static_mesh_component
        c.set_collision_profile_name("BlockAll")
        c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
        c.set_editor_property("visible", False)
        c.set_editor_property("hidden_in_game", True)
        parts.append(finish(col, folder, "COL_" + label, "gate_col", {"y": [y - 15, y + 15], "z": [z0, z1]}))
        mi = unreal.load_asset(NEON_RED)
        h = z1 - bar_z0
        for i, x in enumerate(bars_x):   # 레인 안쪽만(카메라 앞 X < 레인 앞 경계 금지)
            parts.append(finish(neon_cube(unreal.Vector(x, y, bar_z0 + h / 2), unreal.Vector(0.04, 0.04, h / 100), mi), folder, "NEON_%s_%02d" % (label, i), "neon"))
        for zz in (bar_z0 + 4, z1 - 4):
            parts.append(finish(neon_cube(unreal.Vector((lf + LANE_B) / 2, y, zz), unreal.Vector((LANE_B - lf) / 100, 0.06, 0.06), mi),
                                folder, "NEON_%s_Rail_%d" % (label, int(zz)), "neon"))
        return parts

    def terminal(label, name, puzzle, x, y, gate_parts, gate_y):
        z, under = floor_at(x, y)
        t = eas.spawn_actor_from_class(TerminalClass, unreal.Vector(x, y, z), unreal.Rotator(pitch=0, yaw=0, roll=0))
        t.set_editor_property("terminal_name", name)
        t.set_editor_property("puzzle_type", getattr(unreal.HackPuzzleType, puzzle))
        if puzzle == "MEMORY_PATTERN":
            t.set_editor_property("memory_round_lengths", [3, 4, 5])
            t.set_editor_property("memory_relay_range", 900.0)
        mesh = t.get_component_by_class(unreal.StaticMeshComponent)
        mesh.set_static_mesh(unreal.load_asset(TERMINAL_MESH))
        mesh.set_relative_scale3d(unreal.Vector(0.9, 1.4, 1.5))
        mesh.set_relative_location(unreal.Vector(0, 0, 101 * 1.5 / 2), False, False)
        mesh.set_relative_rotation(unreal.Rotator(pitch=0, yaw=90, roll=0), False, False)
        # 메시 지정 뒤 프로퍼티 변경 알림 → OnConstruction 이 충돌 박스를 메시 바운드에 맞춘다(Map1 스크립트와 같음)
        t.set_editor_property("terminal_name", name)
        t.set_editor_property("unlock_actors", gate_parts)
        return finish(t, FOLDER, label, "hackterminal", {"name": name, "puzzle": puzzle, "floor": under, "gate_y": gate_y, "unlocks": len(gate_parts)})

    vz = 1.1 if any(MAP2_VSCALE_TAG in [str(t) for t in a.tags] for a in eas.get_all_level_actors()) else 1.0
    print("vertical scale marker:", vz)
    terms = {}
    for label, name, puzzle, (x, y), (glabel, gy, gz0, gz1, gbz) in TERMINALS:
        gate = laser_gate(glabel, gy, gz0 * vz, gz1 * vz, gbz * vz)
        terms[puzzle] = terminal(label, name, puzzle, x, y, gate, gy)

    for label, y, idx in CLUES:
        z, under = floor_at(0.0, y)   # 그 Y의 오빠 동선 발판(S1 판자 / S2 지붕) 높이 기준
        c = eas.spawn_actor_from_class(ClueClass, unreal.Vector(CLUE_X, y, z + CLUE_H), unreal.Rotator(pitch=0, yaw=180, roll=0))
        c.set_editor_property("terminal", terms["PASSCODE"])
        c.set_editor_property("digit_index", idx)
        finish(c, f"{FOLDER}/Clues", label, "clue", {"digit_index": idx, "floor": under})

    fails = validate(world, eas, ignore)

    path = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map2_Hacking_manifest.json")
    json.dump({"level": LEVEL, "tag": TAG, "actors": manifest}, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("removed", removed, "| placed", len(manifest), "actors ->", path)
    if was_dirty:
        print("NOT SAVED — level was already dirty before this script:", was_dirty)
    elif fails:
        print("NOT SAVED — validation failed")
    else:
        print("saved:", les.save_current_level())


def validate(world, eas, ignore):
    actors = eas.get_all_level_actors()
    fails = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            fails.append(msg)

    def trace(a, b, extra_ignore=()):
        h = unreal.SystemLibrary.line_trace_single(world, a, b, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, list(ignore) + list(extra_ignore),
                                                   unreal.DrawDebugTrace.NONE, True)
        if not h:
            return None, None
        t = h.to_tuple()
        return t[4], t[9]

    cols = set(a.get_actor_label() for a in actors if a.get_actor_label().startswith("COL_"))

    def capsule_hits(x, y, z_feet, hh=88.0):
        hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(x, y, z_feet + hh + 3), 34.0, hh,
                                                           [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], None, [])
        return [h.get_actor_label() for h in (hits or []) if h.get_actor_label() in cols or h.get_class().get_name() == "HackTerminal"]

    terms = [a for a in actors if a.get_class().get_name() == "HackTerminal" and TAG in [str(t) for t in a.tags]]
    kinds = [t.get_editor_property("puzzle_type") for t in terms]
    check(len(terms) == 3 and all(k in kinds for k in (unreal.HackPuzzleType.PASSCODE, unreal.HackPuzzleType.FREQUENCY_LOCK, unreal.HackPuzzleType.MEMORY_PATTERN)),
          f"3 terminals {[ (t.get_actor_label(), str(t.get_editor_property('puzzle_type'))) for t in terms]}")
    spiders = [a for a in actors if a.get_class().get_name() == "SpiderBot" or a.get_actor_label().startswith("Enemy_SpiderBot")]
    for t in terms:
        tl = t.get_actor_location()
        unlock = [a for a in t.get_editor_property("unlock_actors") if a]
        gcol = [a for a in unlock if a.get_actor_label().startswith("COL_HackGate")]
        check(len(gcol) == 1 and len(unlock) >= 8, f"{t.get_actor_label()}: unlocks {len(unlock)} (gate COL {[a.get_actor_label() for a in gcol]})")
        if not gcol:
            continue
        go, ge = gcol[0].get_actor_bounds(False)
        gy0, gy1 = go.y - ge.y, go.y + ge.y
        # 단말기 바닥 / 통로에서 해킹 거리
        fz, under = trace(unreal.Vector(PATH_X, tl.y, tl.z + 300), unreal.Vector(PATH_X, tl.y, tl.z - 300))
        path_z = fz.z if fz else None
        d = ((tl.x - PATH_X) ** 2 + ((path_z or tl.z) + 80 - tl.z) ** 2) ** 0.5
        check(path_z is not None and abs(path_z - tl.z) < 3 and d <= t.get_editor_property("interact_range") - 20 and tl.y < gy0,
              f"  terminal at {[round(tl.x), round(tl.y), round(tl.z)]} on floor (path floor {None if path_z is None else round(path_z)} {under.get_actor_label() if under else None}), "
              f"{round(d)}cm from path X={PATH_X:.0f} (<= {t.get_editor_property('interact_range'):.0f}-20), before gate Y {round(gy0)}..{round(gy1)}")
        box = t.get_component_by_class(unreal.BoxComponent)
        bo, be = box.get_world_location(), box.get_scaled_box_extent()
        check(bo.x - be.x > PATH_X + 34 + 5, f"  terminal body X {round(bo.x - be.x)}..{round(bo.x + be.x)} Y {round(bo.y - be.y)}..{round(bo.y + be.y)} Z {round(bo.z - be.z)}..{round(bo.z + be.z)} leaves path free")
        # 게이트가 통로를 막고, 게이트 밖으로 돌아갈 틈이 없다(앞/뒤 경계벽과 겹침, 점프 높이 이상)
        gz = go.z - ge.z
        okg, lg = trace(unreal.Vector(PATH_X, gy0 - 120, tl.z + 90), unreal.Vector(PATH_X, gy1 + 120, tl.z + 90))
        check(lg is not None and lg.get_actor_label().startswith("COL_HackGate"), f"  gate blocks path trace ({lg.get_actor_label() if lg else None})")
        # 위로 넘을 수 없음: 바닥+300 이상이거나, 게이트 윗면 바로 위가 벽(문 위 벽 등 COL)으로 막혀 있음
        top = go.z + ge.z
        _, cap = trace(unreal.Vector(PATH_X, (gy0 + gy1) / 2, top + 2), unreal.Vector(PATH_X, (gy0 + gy1) / 2, top + 400), [gcol[0]])
        capped = cap is not None and cap.get_actor_label().startswith("COL_") and cap.get_actor_label() != gcol[0].get_actor_label()
        check(go.x - ge.x <= LANE_F - lane_front_delta(eas) - 4 and go.x + ge.x >= LANE_B + 4 and ((top - tl.z) >= 300 or capped) and gz <= tl.z + 1,
              f"  gate COL X {round(go.x - ge.x)}..{round(go.x + ge.x)} Z {round(gz)}..{round(top)} (>= floor+300 above terminal floor {round(tl.z)}, or capped by {cap.get_actor_label() if capped else None})")
        # 단말기 앞 ~ 게이트 사이 레인 앞쪽 통로(X -60)에서 오빠 캡슐이 막히지 않음(30cm 간격)
        blocked = []
        for px in (PATH_X,):
            y = tl.y - 90
            while y <= gy0 - 40:
                p, h = trace(unreal.Vector(px, y, tl.z + 300), unreal.Vector(px, y, tl.z - 400))
                if p:
                    hit = capsule_hits(px, y, p.z + 2)
                    if hit:
                        blocked.append((px, round(y), hit[0]))
                y += 30.0
        check(not blocked, f"  capsule path terminal->gate clear: {blocked[:4]}")
        # 거미 배치와 겹치지 않음(단말기·게이트에서 3m 이상)
        near = []
        for s in spiders:
            so, se = s.get_actor_bounds(False)
            for (cy, cz, nm) in ((tl.y, tl.z, "terminal"), ((gy0 + gy1) / 2, gz, "gate")):
                if abs(so.y - cy) < se.y + 300 and abs(so.z - se.z - cz) < 300:
                    near.append((s.get_actor_label(), nm, round(so.y)))
        check(not near, f"  spiders clear of terminal/gate: {near}")
        if t.get_editor_property("puzzle_type") == unreal.HackPuzzleType.MEMORY_PATTERN:
            # 오빠가 설 수 있는 게이트 앞 발판 중 중계 범위(9m) 안인 지점
            rr = t.get_editor_property("memory_relay_range")
            spots = []
            y = gy0 - 60
            while y > gy0 - 1500:
                p, h = trace(unreal.Vector(PATH_X, y, tl.z + 600), unreal.Vector(PATH_X, y, tl.z - 400))
                if p and not capsule_hits(PATH_X, y, p.z + 2):
                    dd = ((PATH_X - tl.x) ** 2 + (y - tl.y) ** 2 + (p.z + 88 - tl.z) ** 2) ** 0.5
                    if dd <= rr - 100:
                        spots.append(round(y))
                y -= 50.0
            check(len(spots) >= 5, f"  memory relay: brother standable spots within {rr - 100:.0f}cm of terminal before gate: {len(spots)} (Y {min(spots) if spots else None}..{max(spots) if spots else None})")

    clues = [a for a in actors if a.get_class().get_name() == "HackCodeClue" and TAG in [str(t) for t in a.tags]]
    pt = [t for t in terms if t.get_editor_property("puzzle_type") == unreal.HackPuzzleType.PASSCODE]
    check(len(clues) == 4 and pt and all(c.get_editor_property("terminal") and c.get_editor_property("terminal").get_actor_label() == pt[0].get_actor_label() for c in clues)
          and sorted(c.get_editor_property("digit_index") for c in clues) == [0, 1, 2, 3]
          and all(c.get_actor_location().y < pt[0].get_actor_location().y for c in clues)
          and all(abs(abs(c.get_actor_rotation().yaw) - 180) < 1 for c in clues),
          f"4 clues -> passcode terminal, digits 0..3, before terminal, yaw 180: {[(c.get_actor_label(), c.get_editor_property('digit_index'), round(c.get_actor_location().y)) for c in clues]}")
    print("RESULT:", "ALL PASS" if not fails else f"{len(fails)} FAIL")
    return fails


def run():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    cur = les.get_current_level().get_outermost().get_name() if les.get_current_level() else ""
    if cur != LEVEL:
        unreal.SystemLibrary.collect_garbage()
        print("load_level:", les.load_level(LEVEL), "— run this script again to place")
        return
    place()


run()
