"""
Map1_SoulCity에 해킹 시스템(.claude/Plans/해킹 시스템 기획.md 6장)을 추가한다.

- 태그 GEN_Map1Hack 이 붙은 액터만 지우고 다시 만든다(레벨의 다른 액터는 건드리지 않음 → 사람이 다듬은 레벨에도 안전).
- 비밀번호 단말기(S3 타워 L2 발판) + S4 입구 레이저 게이트
- 주파수 동조 단말기(S4 코어) + S5 탈출로 레이저 게이트 (화살표 시퀀스는 적 해킹 전용으로 바뀌어 단말기에서 폐기 — 기획 0-1장)
- 비밀번호 숫자 단서 4개(오빠 화면 전용) — S1~S3 레인 뒤 벽
바닥 높이는 COL 박스에 라인트레이스로 구한다.
"""
import unreal

LEVEL = "/Game/Project_CX/Maps/Map1_SoulCity"
TAG = "GEN_Map1Hack"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TERMINAL_MESH = "/Game/SoulCity/Environment/Meshes/Building_Slum/SM_Slums_Vent_02b"
NEON_RED = "/Game/Project_CX/Environment/Materials/MI_Neon_Red"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if les.get_current_level().get_outermost().get_name() != LEVEL:
    les.load_level(LEVEL)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

for a in eas.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
        eas.destroy_actor(a)

TerminalClass = unreal.load_class(None, "/Script/Project_CX.HackTerminal")
ClueClass = unreal.load_class(None, "/Script/Project_CX.HackCodeClue")


PROP_BOXES = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("COL_Prop_") or a.get_class() == TerminalClass]


def floor_at(x, y):
    """지형 발판 높이(소품 충돌 박스는 무시)."""
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 3000), unreal.Vector(x, y, -500),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, PROP_BOXES, unreal.DrawDebugTrace.NONE, True)
    if not hit:
        raise RuntimeError("no floor at %s,%s" % (x, y))
    t = hit.to_tuple()
    return t[4].z, t[9].get_actor_label()


def finish(a, folder, label):
    a.set_folder_path(folder)
    a.set_actor_label(label)
    a.tags = [TAG]
    return a


def terminal(label, x, y, name, puzzle, folder):
    z, under = floor_at(x, y)
    t = eas.spawn_actor_from_class(TerminalClass, unreal.Vector(x, y, z), unreal.Rotator(pitch=0, yaw=0, roll=0))
    t.set_editor_property("terminal_name", name)
    t.set_editor_property("puzzle_type", puzzle)
    mesh = t.get_component_by_class(unreal.StaticMeshComponent)
    mesh.set_static_mesh(unreal.load_asset(TERMINAL_MESH))
    # 바닥 위에 세운 단말기 박스(100x34x101 → 세로로 늘림), 화면을 향하도록 yaw 90
    mesh.set_relative_scale3d(unreal.Vector(0.9, 1.4, 1.5))
    mesh.set_relative_location(unreal.Vector(0, 0, 101 * 1.5 / 2), False, False)
    mesh.set_relative_rotation(unreal.Rotator(pitch=0, yaw=90, roll=0), False, False)
    # 메시 지정 후 프로퍼티 변경 알림으로 구성 스크립트(AHackTerminal::OnConstruction)를 다시 돌려 충돌 박스를 메시 바운드에 맞춘다
    t.set_editor_property("terminal_name", name)
    box = t.get_component_by_class(unreal.BoxComponent)
    print("terminal", label, "at z", round(z), "on", under, "| collision extent", box.get_unscaled_box_extent())
    return finish(t, folder, label)


# 2026-11-04: widen_lane_front.py 로 레인 앞(카메라 쪽)이 넓어졌으면(표식 액터 태그 'LaneFrontDelta=<cm>', Map1 1.1배 = 52)
# 게이트 COL 앞 끝·레일을 그만큼 앞으로 늘리고, 넓어진 폭에 같은 간격(50)으로 막대를 더한다 → 재실행해도 레인 전체를 막는다.
LANE_FRONT_TAG = "LaneFrontDelta="
LD = 0.0
for _a in eas.get_all_level_actors():
    for _t in _a.tags:
        if str(_t).startswith(LANE_FRONT_TAG):
            LD = float(str(_t)[len(LANE_FRONT_TAG):])
GX0, GX1 = -250.0 - LD, 250.0          # 게이트 COL·레일 X 범위(바닥 COL 과 같은 앞 끝)
BARS_X = [float(x) for x in range(-225, 251, 50)]
while BARS_X[0] - 50 >= -260.0 - LD + 5:   # 새 앞 경계(-260 - LD) 안쪽만
    BARS_X.insert(0, BARS_X[0] - 50)


def laser_gate(label, y, z0, z1, folder):
    """레인 전체(X GX0~250, 원래 -250~250)를 막는 투명 충돌 + 붉은 네온 막대."""
    parts = []
    col = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((GX0 + GX1) / 2, y, (z0 + z1) / 2))
    col.set_actor_scale3d(unreal.Vector((GX1 - GX0) / 100, 0.3, (z1 - z0) / 100))
    c = col.static_mesh_component
    c.set_collision_profile_name("BlockAll")
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property("visible", False)
    c.set_editor_property("hidden_in_game", True)
    parts.append(finish(col, folder, "COL_" + label))
    mi = unreal.load_asset(NEON_RED)
    i = 0
    for x in BARS_X:
        bar = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector(x, y, (z0 + z1) / 2))
        bar.set_actor_scale3d(unreal.Vector(0.04, 0.04, (z1 - z0) / 100))
        sm = bar.static_mesh_component
        sm.set_material(0, mi)
        sm.set_collision_profile_name("NoCollision")
        sm.set_editor_property("cast_shadow", False)
        parts.append(finish(bar, folder, "NEON_%s_%02d" % (label, i)))
        i += 1
    for zz in (z0 + 4, z1 - 4):
        rail = eas.spawn_actor_from_object(unreal.load_asset(CUBE), unreal.Vector((GX0 + GX1) / 2, y, zz))
        rail.set_actor_scale3d(unreal.Vector((GX1 - GX0) / 100, 0.06, 0.06))
        rail.static_mesh_component.set_material(0, mi)
        rail.static_mesh_component.set_collision_profile_name("NoCollision")
        parts.append(finish(rail, folder, "NEON_%s_Rail_%d" % (label, int(zz))))
    return parts


def clue(label, x, y, height, term, index, folder):
    z, _ = floor_at(0, y)   # 레인 발판 높이 기준
    # TextRender는 +X 쪽에서 읽히므로, 카메라(-X)에서 바로 보이게 yaw 180
    c = eas.spawn_actor_from_class(ClueClass, unreal.Vector(x, y, z + height), unreal.Rotator(pitch=0, yaw=180, roll=0))
    c.set_editor_property("terminal", term)
    c.set_editor_property("digit_index", index)
    return finish(c, folder, label)


# 2026-11-04: Map1 은 scale_map1_vertical.py 로 세로 1.1배가 됐다(태그 GEN_Map2VScale 표식 액터 — 두 맵 공용 태그).
# 바닥은 트레이스로 구하므로 그대로 맞고, 게이트 높이 320 만 1.1배(352)로 맞춘다. 단서 높이 230 은 유지(1.1배 스크립트도 바닥 위 높이를 유지했다).
VZ = 1.1 if any("GEN_Map2VScale" in [str(t) for t in a.tags] for a in eas.get_all_level_actors()) else 1.0
GATE_H = 320.0 * VZ

# ---------------- 비밀번호 단말기 + S4 입구 게이트
T1 = terminal("HackTerminal_Passcode", 200, 7150, "TERMINAL_SEC-7", unreal.HackPuzzleType.PASSCODE, "Gameplay/Hacking")
z_top, _ = floor_at(0, 8300)
gate1 = laser_gate("HackGate_S4Entry", 8260, z_top, z_top + GATE_H, "Gameplay/Hacking/Gate_S4Entry")
T1.set_editor_property("unlock_actors", gate1)

# ---------------- 주파수 동조 단말기(코어) + S5 탈출로 게이트 — 구간 폭·속도·제한 시간은 AHackTerminal 기본값(24/18/13%, 1.5/2.0/2.6, 제한 없음)
T2 = terminal("HackTerminal_Core", 200, 9770, "CORE_MAINFRAME", unreal.HackPuzzleType.FREQUENCY_LOCK, "Gameplay/Hacking")
z_core, _ = floor_at(0, 9900)
gate2 = laser_gate("HackGate_S5Exit", 9985, z_core, z_core + GATE_H, "Gameplay/Hacking/Gate_S5Exit")
T2.set_editor_property("unlock_actors", gate2)

# ---------------- 숫자 단서(오빠 화면 전용): 오빠 동선 S1~S3, 자리 순서는 섞어서 배치
for label, y, idx in [("HackClue_S1", 1300, 1), ("HackClue_S2_Street", 4300, 3), ("HackClue_S2_Terrace", 5400, 0), ("HackClue_S3", 6450, 2)]:
    clue(label, 240, y, 230, T1, idx, "Gameplay/Hacking/Clues")

les.save_current_level()
print("hacking placed:", len([a for a in eas.get_all_level_actors() if TAG in [str(t) for t in a.tags]]), "actors")
