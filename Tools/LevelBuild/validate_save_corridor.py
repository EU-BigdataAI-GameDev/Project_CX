"""
세이브 복도 검증 — build_save_corridor.py 결과를 저장된 레벨에서 다시 확인한다(에디터 원격 실행).

1회차: 현재 레벨이 복도가 아니면 불러오고 끝(로드는 자기 호출에서만). 2회차: 검사.
검사: manifest 일치 / GameMode·KillZ / PlayerStart 2개(하나는 Sister)가 바닥 위 / COL 설정(Pawn 막음·Camera 무시·숨김) /
      아트 NoCollision / 카메라 앞(X < LANE_F) 가림 / 레인 전 구간 바닥 연속(간격·턱 없음) / 양 끝·앞뒤 막힘 /
      오빠·여동생 캡슐로 시작 → 세이브포인트 → 출구까지 레인 앞쪽 길이 막히지 않음 / 세이브포인트·출구 설정과 도달 가능 /
      세이브 단말기: 대표 에셋(SM_glowstick02 빛 기둥) 사용·몸체 충돌(Pawn 막음·Camera/Visibility 무시)·이어하기 스폰 자리가 몸체와 안 겹침.
      v1.3(2026-11-04): 해킹 단말기·레이저 게이트 없음(HackTerminal 0개, HackGate 액터 0개) — 시작 → 출구까지 캡슐 통로 막힘 0.
      세이브는 상호작용(F/X): auto_save_on_enter 꺼짐, 통로(X -60)가 세이브 구역 안이라 지나가며 F/X를 누를 수 있음.
"""
import unreal, json, math

LEVEL = "/Game/Project_CX/Maps/Map_SaveCorridor"
TAG = "GEN_SaveCorridor"
# build_save_corridor.py 와 같은 값(앞쪽 넓히기: -120 ~ 150 을 카메라 쪽만 — 2026-11-04 1.1배 -147, 2026-11-06 1.5배 → -255 ~ 150)
DEPTH_FACTOR = 1.5
LANE_B = 150.0
LANE_F = round(LANE_B - (LANE_B - (-120.0)) * DEPTH_FACTOR, 1)
Y_START, Y_END = -400.0, 1800.0     # 22m(2026-11-04 단축), 바닥 0 평지
FLOOR_Z = 0.0
STEP_MAX = 45.0     # 캐릭터 자동 계단 높이(실측 45)


def validate():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = eas.get_all_level_actors()
    gen = [a for a in actors if TAG in [str(t) for t in a.tags]]
    m = json.load(open(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Docs/LevelManifests/Map_SaveCorridor_manifest.json"), encoding="utf-8"))
    fails = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            fails.append(msg)

    check(len(gen) == len(m["actors"]), f"generated {len(gen)} = manifest {len(m['actors'])}")
    ws = world.get_world_settings()
    gm = ws.get_editor_property("default_game_mode")
    check(gm is not None and gm.get_name() == "BP_GameMode_C", f"GameMode {gm.get_name() if gm else None}")
    check(abs(ws.get_editor_property("kill_z") + 1500.0) < 1, f"KillZ {ws.get_editor_property('kill_z')}")

    starts = [a for a in actors if isinstance(a, unreal.PlayerStart)]
    tags = sorted(str(a.get_editor_property("player_start_tag")) for a in starts)
    check(len(starts) == 2 and tags.count("Sister") == 1, f"PlayerStarts {[(a.get_actor_label(), str(a.get_editor_property('player_start_tag'))) for a in starts]}")

    def trace(a, b, ignore=()):
        h = unreal.SystemLibrary.line_trace_single(world, a, b, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, list(ignore),
                                                   unreal.DrawDebugTrace.NONE, True)
        if not h:
            return None, None
        t = h.to_tuple()
        return t[4], t[9]

    for a in starts:
        l = a.get_actor_location()
        hh = 80.0 if str(a.get_editor_property("player_start_tag")) == "Sister" else 88.0
        p, hit = trace(unreal.Vector(l.x, l.y, l.z), unreal.Vector(l.x, l.y, l.z - 400), starts)
        gap = (l.z - hh) - p.z if p else None
        check(p is not None and hit.get_actor_label().startswith("COL_") and gap is not None and -5 <= gap <= 10,
              f"{a.get_actor_label()} on floor (feet-floor gap {None if gap is None else round(gap, 1)} on {hit.get_actor_label() if hit else None})")

    # ---------------------------------------------------------------- 충돌/가시성
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
    for a in gen:
        if isinstance(a, unreal.TextRenderActor) and str(a.get_editor_property("text_render").get_collision_profile_name()) != "NoCollision":
            bad.append(a.get_actor_label())
    check(not bad, f"COL boxes {len(cols)} (Pawn block / Camera ignore / hidden), art+text NoCollision — bad: {bad[:8]}")

    # ---------------------------------------------------------------- 바닥 연속성(레인 3줄, 20cm 간격)
    def floor_at(x, y):
        p, hit = trace(unreal.Vector(x, y, 2000), unreal.Vector(x, y, -1000))
        return (p.z, hit.get_actor_label()) if p else (None, None)
    profile = {}
    for x in (LANE_F + 22.0, LANE_F + 36.0, -80.0, 0.0, 110.0):     # 앞 두 줄 = 새 앞 가장자리(경계 바로 안 / 캡슐 중심)
        prev, gaps, steps = None, [], []
        y = Y_START + 40
        while y <= Y_END - 40:
            z, lab = floor_at(x, y)
            if z is None or not lab.startswith("COL_") or lab.startswith("COL_Bound"):
                gaps.append((round(y), lab))
            else:
                if lab.startswith("COL_Prop"):
                    pass   # 소품 위 — 막힘 검사에서 따로 본다
                elif prev is not None and abs(z - prev) > STEP_MAX:
                    steps.append((round(y), round(prev), round(z)))
                if not lab.startswith("COL_Prop"):
                    prev = z
                    profile.setdefault(x, []).append((y, z))
            y += 20.0
        check(not gaps and not steps, f"floor continuous at X={x:.0f}: gaps {gaps[:5]} steps>{STEP_MAX:.0f} {steps[:5]}")
    zs = [z for _, z in profile.get(0.0, [])]
    print("      floor heights along lane:", sorted(set(round(z) for z in zs))[:3], "...", "min", round(min(zs)), "max", round(max(zs)))

    # ---------------------------------------------------------------- 막힘(양 끝, 앞, 뒤)
    def blocked_by(a, b, prefix):
        p, hit = trace(a, b)
        return hit is not None and hit.get_actor_label().startswith(prefix), (hit.get_actor_label() if hit else None)
    ok, lab = blocked_by(unreal.Vector(0, Y_START + 150, 90), unreal.Vector(0, Y_START - 500, 90), "COL_Bound_Start")
    check(ok, f"start end closed ({lab})")
    ok, lab = blocked_by(unreal.Vector(0, Y_END - 150, 90), unreal.Vector(0, Y_END + 500, 90), "COL_Bound_End")
    check(ok, f"far end closed ({lab})")
    for y, z in ((0, 90), (600, 90), (1500, 90)):
        okf, lf = blocked_by(unreal.Vector(0, y, z), unreal.Vector(-600, y, z), "COL_Bound_Front")
        okb, lb = blocked_by(unreal.Vector(-60, y, z), unreal.Vector(800, y, z), "COL_")   # 세이브 단말기 충돌은 Visibility 무시라 뒤 경계까지 간다
        pf, _ = trace(unreal.Vector(0, y, z), unreal.Vector(-600, y, z))
        check(okf and okb and pf is not None and abs(pf.x - LANE_F) < 1.0, f"lane sides closed at Y={y} (front {lf} at X {None if pf is None else round(pf.x, 1)} = LANE_F {LANE_F}, back {lb})")

    # ---------------------------------------------------------------- 카메라 앞 가림(X < LANE_F 에 그 Y의 발판 높이를 넘는 메시 금지, 레인 Y 범위 안만)
    prof0 = profile.get(0.0, [])
    def floor_top(y0, y1):
        tops = [z for y, z in prof0 if y0 - 20 <= y <= y1 + 20]
        return max(tops) if tops else None
    viol = []
    for a in gen:
        if a.get_actor_label().startswith("COL_") or not isinstance(a, unreal.StaticMeshActor):
            continue
        o, e = a.get_actor_bounds(False)
        if o.x - e.x < LANE_F - 5 and o.y + e.y > Y_START + 1 and o.y - e.y < Y_END - 1:   # 양 끝 마감벽(레인 밖)은 제외
            top = floor_top(o.y - e.y, o.y + e.y)
            if top is None or o.z + e.z > top + 5:
                viol.append((a.get_actor_label(), round(o.x - e.x), round(o.z + e.z), top))
    check(not viol, f"front-occluder violations {len(viol)} {viol[:6]}")

    # ---------------------------------------------------------------- 해킹 단말기·게이트 없음(v1.3) + 캡슐 통과(레인 앞쪽 X=-60 줄, 30cm 간격)
    terms = [a for a in actors if a.get_class().get_name() == "HackTerminal"]
    gate_any = [a.get_actor_label() for a in actors if "HackGate" in a.get_actor_label() or "PitGate" in a.get_actor_label()]
    check(not terms and not gate_any, f"no hack terminal / gate in corridor: terminals {[a.get_actor_label() for a in terms]} gate {gate_any[:4]}")
    col_set = set(a.get_actor_label() for a in cols)
    def overlap(x, y, z_feet, hh):
        hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(x, y, z_feet + hh + 3), 34.0, hh,
                                                           [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], unreal.StaticMeshActor, [])
        return [h.get_actor_label() for h in (hits or []) if h.get_actor_label() in col_set]
    for who, hh in (("sister", 80.0), ("brother", 88.0)):
        blocked = []
        y = Y_START + 40
        while y <= Y_END - 40:
            for px in (-60.0, LANE_F + 36.0):      # 통로 + 새 앞 가장자리 줄
                z, lab = floor_at(px, y)
                if z is not None:
                    h = overlap(px, y, z + 3, hh)
                    if h:
                        blocked.append((round(px), round(y), h[0]))
            y += 30.0
        check(not blocked, f"{who} capsule path start->exit at X=-60 and X={LANE_F + 36:.0f} (front edge) clear: blocked {blocked[:5]}")

    # ---------------------------------------------------------------- 세이브포인트 / 출구
    sps = [a for a in actors if a.get_class().get_name() == "CXSavePoint"]
    exs = [a for a in actors if a.get_class().get_name() == "CXLevelExit"]
    check(len(sps) == 1 and str(sps[0].get_editor_property("save_point_id")) == "SP_Corridor_01", f"save point {[a.get_actor_label() for a in sps]}")
    check(len(sps) == 1 and not sps[0].get_editor_property("auto_save_on_enter"), f"save point saves by interact (auto_save_on_enter={sps[0].get_editor_property('auto_save_on_enter') if sps else None})")
    check(len(exs) == 1 and exs[0].get_next_map_package() == "/Game/Project_CX/Maps/Map3_Final", f"exit next={exs[0].get_next_map_package() if exs else None}")
    for a in sps + exs:
        l = a.get_actor_location()
        zx = a.get_editor_property("zone_extent")
        z, lab = floor_at(l.x, l.y)
        path_in_zone = (l.x - zx.x) <= LANE_F + 34.0 and -60.0 <= (l.x + zx.x)   # 새 앞 가장자리 캡슐 중심까지 덮음
        check(z is not None and abs(z - l.z) < 2 and path_in_zone and Y_START < l.y - zx.y and l.y + zx.y < Y_END,
              f"{a.get_actor_label()} at {[round(l.x), round(l.y), round(l.z)]} on floor {z} ({lab}), zone X {round(l.x - zx.x)}..{round(l.x + zx.x)} covers path, "
              f"Y {round(l.y - zx.y)}..{round(l.y + zx.y)}")
    check(all(abs(z - FLOOR_Z) < 1 for z in zs), f"flat floor at Z {FLOOR_Z:.0f} (no stairs)")

    # ---------------------------------------------------------------- 세이브 단말기(대표 에셋 / 몸체 충돌 / 스폰 자리)
    if sps:
        sp = sps[0]
        dbg = sp.get_debug_state() if hasattr(sp, "get_debug_state") else ""
        pm = sp.get_editor_property("pylon_mesh")
        check(pm is not None and pm.get_name() == "SM_glowstick02" and "terminal=SM_glowstick02" in dbg, f"terminal asset {pm.get_name() if pm else None} ({dbg.split('terminal=')[-1] if dbg else '?'})")
        bc = sp.get_editor_property("body_collision")
        check(bc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) == unreal.CollisionResponseType.ECR_BLOCK and
              bc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA) == unreal.CollisionResponseType.ECR_IGNORE and
              bc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY) == unreal.CollisionResponseType.ECR_IGNORE and
              bc.is_collision_enabled(), "terminal body collision: Pawn block / Camera+Visibility ignore")
        o, e = bc.get_world_location(), bc.get_scaled_box_extent()
        sl = sp.get_actor_location()
        sd, ss = sp.get_editor_property("spawn_depth"), sp.get_editor_property("spawn_spacing")
        spawn_x = sl.x + sd
        clear = spawn_x + 34 < o.x - e.x and LANE_F < spawn_x - 34
        check(clear and o.x + e.x <= LANE_B + 1, f"terminal body X {round(o.x - e.x)}..{round(o.x + e.x)} Y {round(o.y - e.y)}..{round(o.y + e.y)} Z ..{round(o.z + e.z)}; "
              f"continue spawns X {round(spawn_x)} Y {round(sl.y - ss)}/{round(sl.y + ss)} clear of body")
        col_set2 = set(a.get_actor_label() for a in cols)
        for who, hh, dy in (("brother", 88.0, -ss), ("sister", 80.0, ss)):
            hits = unreal.SystemLibrary.capsule_overlap_actors(world, unreal.Vector(spawn_x, sl.y + dy, FLOOR_Z + hh + 3), 34.0, hh,
                                                               [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], None, [])
            names = [h.get_actor_label() for h in (hits or []) if h.get_actor_label() in col_set2 or h == sp]
            check(not names, f"continue spawn {who} capsule free: {names}")
    print("RESULT:", "ALL PASS" if not fails else f"{len(fails)} FAIL")


def run():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    cur = les.get_current_level().get_outermost().get_name() if les.get_current_level() else ""
    if cur != LEVEL:
        unreal.SystemLibrary.collect_garbage()
        print("load_level:", les.load_level(LEVEL), "— run this script again to validate")
        return
    validate()


run()
