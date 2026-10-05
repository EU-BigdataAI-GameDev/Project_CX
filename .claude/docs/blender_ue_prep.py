"""
blender_ue_prep.py — Mixamo 리그 캐릭터를 언리얼용 FBX로 준비하는 재사용 스크립트

사용법:
    1) 아래 CONFIG 를 캐릭터에 맞게 수정
    2) Blender 텍스트 에디터에 붙여넣고 실행 (또는 MCP execute_blender_code)

동작:
    원본을 건드리지 않고 UE_Export 컬렉션에 복제본을 만들어
    스케일 정규화 → 트랜스폼 적용 → 액션 커브 보정 → 루트 본 추가 → 익스포트 → 검증

전제:
    - Mixamo 표준 리그 (mixamorig: 프리픽스)
    - 메시가 아마추어에 부모로 물려 있고 ARMATURE 모디파이어가 걸려 있음
    - 캐릭터가 Blender -Y 방향을 바라봄 (Mixamo 기본)
"""

import bpy
import os
import math
import struct
import zlib
from mathutils import Vector

# ─────────────────────────────────────────────────────────────
# CONFIG
# ─────────────────────────────────────────────────────────────
CONFIG = {
    "character":   "Brother",          # 애셋 프리픽스로 쓰임
    "armature":    "Armature",         # 씬의 아마추어 오브젝트 이름
    "mesh":        "node_0",           # 씬의 스킨 메시 오브젝트 이름
    "target_height_m": 1.80,           # 목표 키 (m). UE5 마네킹 ≈ 1.80
    "add_root_bone": True,             # 루트 모션 / IK 리타게팅 대응
    "root_bone_length_m": 0.20,
    "out_dir":     r"C:/Users/.../Art/Source/Brother",
    "export_sk":   True,               # 스켈레탈 메시 FBX 뽑기
    "actions":     [],                 # 내보낼 액션 이름 목록. [] 이면 전부
    "verify_frames": 5,                # 검증에 쓸 프레임 개수
}

ROOT = "root"
HIPS = "mixamorig:Hips"


# ─────────────────────────────────────────────────────────────
# 유틸
# ─────────────────────────────────────────────────────────────
def iter_fcurves(action):
    """Blender 4.4+ 슬롯 액션과 구버전 모두 지원"""
    try:
        for fc in action.fcurves:
            yield fc
        return
    except AttributeError:
        pass
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    yield fc


def assign_action(obj, action):
    if obj.animation_data is None:
        obj.animation_data_create()
    obj.animation_data.action = action
    try:
        obj.animation_data.action_slot = action.slots[0]
    except Exception:
        pass


def rest_height(mesh_obj):
    """레스트 포즈(모디파이어 미적용) 월드 높이"""
    mw = mesh_obj.matrix_world
    zs = [(mw @ v.co).z for v in mesh_obj.data.vertices]
    return max(zs) - min(zs)


def deformed_bbox(mesh_obj):
    """디폼 적용 후 월드 바운딩박스"""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = mesh_obj.evaluated_get(dg)
    me = ev.to_mesh()
    mw = ev.matrix_world
    co = [mw @ v.co for v in me.vertices]
    r = (min(c.x for c in co), max(c.x for c in co),
         min(c.y for c in co), max(c.y for c in co),
         min(c.z for c in co), max(c.z for c in co))
    ev.to_mesh_clear()
    return r


def arm_angle(arm_obj, side):
    """어깨->손 벡터가 수직선에서 벌어진 각도 + X 부호 (월드 기준)"""
    mw = arm_obj.matrix_world
    a = mw @ arm_obj.pose.bones["mixamorig:%sArm" % side].matrix.to_translation()
    h = mw @ arm_obj.pose.bones["mixamorig:%sHand" % side].matrix.to_translation()
    d = h - a
    ang = math.degrees(math.atan2(math.hypot(d.x, d.y), abs(d.z)))
    return ang, d.x


def scale_location_curves(action, factor):
    n = 0
    for fc in iter_fcurves(action):
        if not fc.data_path.endswith(".location"):
            continue
        for kp in fc.keyframe_points:
            kp.co.y *= factor
            kp.handle_left.y *= factor
            kp.handle_right.y *= factor
        fc.update()
        n += 1
    return n


# ─────────────────────────────────────────────────────────────
# FBX 바이너리 검증
# ─────────────────────────────────────────────────────────────
def fbx_parse(path):
    buf = open(path, "rb").read()
    wide = struct.unpack("<I", buf[23:27])[0] >= 7500
    ARRT = {b'f': ('f', 4), b'd': ('d', 8), b'l': ('q', 8), b'i': ('i', 4), b'b': ('b', 1)}
    SCAT = {b'Y': ('h', 2), b'C': ('?', 1), b'I': ('i', 4), b'F': ('f', 4), b'D': ('d', 8), b'L': ('q', 8)}
    HDR = 25 if wide else 13

    def read_prop(b, o):
        t = b[o:o + 1]; o += 1
        if t in SCAT:
            f, s = SCAT[t]
            return struct.unpack("<" + f, b[o:o + s])[0], o + s
        if t in ARRT:
            f, s = ARRT[t]
            n, enc, cl = struct.unpack("<III", b[o:o + 12]); o += 12
            raw = b[o:o + cl]; o += cl
            if enc == 1:
                raw = zlib.decompress(raw)
            return ('ARR', f, n, raw), o
        n = struct.unpack("<I", b[o:o + 4])[0]; o += 4
        return b[o:o + n], o + n

    def read_node(b, o):
        if wide:
            end, np_, pl = struct.unpack("<QQQ", b[o:o + 24]); o += 24
        else:
            end, np_, pl = struct.unpack("<III", b[o:o + 12]); o += 12
        nl = b[o]; o += 1
        name = b[o:o + nl]; o += nl
        if end == 0:
            return None, 0
        props = []
        for _ in range(np_):
            p, o = read_prop(b, o)
            props.append(p)
        kids = []
        while o < end - HDR:
            c, o = read_node(b, o)
            if c is None:
                break
            kids.append(c)
        return {"name": name, "props": props, "children": kids}, end

    o, roots = 27, []
    while o < len(buf) - HDR:
        n, o = read_node(buf, o)
        if n is None:
            break
        roots.append(n)
    return buf, roots


def fbx_report(path):
    buf, roots = fbx_parse(path)
    out = {"file": os.path.basename(path), "size_mb": round(len(buf) / 1048576, 2)}

    gs = next(r for r in roots if r["name"] == b"GlobalSettings")
    for pr in next(c for c in gs["children"] if c["name"] == b"Properties70")["children"]:
        if pr["props"][0] == b"UnitScaleFactor":
            out["UnitScaleFactor"] = round(float(pr["props"][-1]), 4)

    objs = next(r for r in roots if r["name"] == b"Objects")
    out["node_scaling"] = {}
    out["bone_translation"] = {}
    for m in objs["children"]:
        if m["name"] != b"Model":
            continue
        nm = m["props"][1].decode(errors="ignore").split("\x00")[0]
        p70 = next((x for x in m["children"] if x["name"] == b"Properties70"), None)
        if not p70:
            continue
        for pr in p70["children"]:
            if pr["props"][0] == b"Lcl Scaling":
                v = tuple(round(float(x), 3) for x in pr["props"][-3:])
                if v != (1.0, 1.0, 1.0):
                    out["node_scaling"][nm] = v
            if pr["props"][0] == b"Lcl Translation":
                out["bone_translation"][nm] = tuple(round(float(x), 4) for x in pr["props"][-3:])

    smax = 0.0
    scount = 0
    for c in objs["children"]:
        if c["name"] != b"AnimationCurveNode":
            continue
        if c["props"][1].decode(errors="ignore").split("\x00")[0] != "S":
            continue
        scount += 1
        p70 = next((x for x in c["children"] if x["name"] == b"Properties70"), None)
        if p70:
            for pr in p70["children"]:
                if pr["props"][0].decode(errors="ignore").startswith("d|"):
                    smax = max(smax, abs(float(pr["props"][-1])))
    out["scale_curve_count"] = scount
    out["scale_curve_max"] = round(smax, 4)

    for k in (b"Geometry", b"Deformer", b"Skin", b"Cluster", b"BindPose",
              b"LimbNode", b"AnimationCurve", b"LayerElementSmoothing",
              b"LayerElementNormal", b"Material", b"Texture"):
        out[k.decode()] = buf.count(k)
    return out


# ─────────────────────────────────────────────────────────────
# 메인
# ─────────────────────────────────────────────────────────────
def prepare(cfg):
    scn = bpy.context.scene
    src_arm = bpy.data.objects[cfg["armature"]]
    src_mesh = bpy.data.objects[cfg["mesh"]]
    name = cfg["character"]
    os.makedirs(cfg["out_dir"], exist_ok=True)

    log = []
    def p(s):
        log.append(s)
        print(s)

    # 원본 상태 저장
    prev_action = src_arm.animation_data.action if src_arm.animation_data else None
    prev_frame = scn.frame_current
    prev_pose = src_arm.data.pose_position

    # 내보낼 액션 결정
    actions = cfg["actions"] or [a.name for a in bpy.data.actions
                                 if a.users > 0 and a.frame_range[1] - a.frame_range[0] > 2]
    p("대상 액션: %s" % actions)

    # 배율 계산
    h = rest_height(src_mesh)
    S = cfg["target_height_m"] / h
    p("레스트 키 %.6f m -> 목표 %.2f m / 배율 %.6f" % (h, cfg["target_height_m"], S))

    # 컬렉션
    coll = bpy.data.collections.get("UE_Export")
    if coll is None:
        coll = bpy.data.collections.new("UE_Export")
        scn.collection.children.link(coll)

    # 복제
    d_arm = src_arm.copy()
    d_arm.data = src_arm.data.copy()
    d_arm.name = "UE_%s_Armature" % name
    coll.objects.link(d_arm)

    d_mesh = src_mesh.copy()
    d_mesh.data = src_mesh.data.copy()
    d_mesh.name = "UE_%s_Mesh" % name
    coll.objects.link(d_mesh)

    wm = d_mesh.matrix_world.copy()
    d_mesh.parent = None
    d_mesh.matrix_world = wm
    for m in d_mesh.modifiers:
        if m.type == 'ARMATURE':
            m.object = d_arm

    # 기준값 기록 (첫 액션 기준)
    base = {}
    frames = []
    if actions:
        a0 = bpy.data.actions[actions[0]]
        assign_action(d_arm, a0)
        fs, fe = int(a0.frame_range[0]), int(a0.frame_range[1])
        step = max(1, (fe - fs) // max(1, cfg["verify_frames"] - 1))
        frames = list(range(fs, fe + 1, step))[:cfg["verify_frames"]]
        for f in frames:
            scn.frame_set(f)
            base[f] = deformed_bbox(d_mesh)

    # 스케일 + 회전 적용
    d_arm.scale = (S, S, S)
    d_mesh.scale = (S, S, S)
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    d_arm.select_set(True)
    d_mesh.select_set(True)
    bpy.context.view_layer.objects.active = d_arm
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    p("트랜스폼 적용 완료: rot=%s scale=%s" %
      ([round(v, 3) for v in d_arm.rotation_euler], [round(v, 4) for v in d_arm.scale]))

    d_mesh.parent = d_arm
    d_mesh.matrix_parent_inverse = d_arm.matrix_world.inverted()

    # 액션 복제 + location 커브 보정
    new_actions = {}
    for an in actions:
        src_act = bpy.data.actions[an]
        na = src_act.copy()
        na.name = "UE_%s_%s" % (name, an)
        n = scale_location_curves(na, S)
        new_actions[an] = na
        p("  액션 %-28s location 커브 %d개 보정 (x%.6f)" % (an, n, S))

    # 루트 본
    if cfg["add_root_bone"]:
        bpy.ops.object.select_all(action='DESELECT')
        d_arm.select_set(True)
        bpy.context.view_layer.objects.active = d_arm
        bpy.ops.object.mode_set(mode='EDIT')
        eb = d_arm.data.edit_bones
        if ROOT in eb:
            eb.remove(eb[ROOT])
        r = eb.new(ROOT)
        r.head = (0.0, 0.0, 0.0)
        r.tail = (0.0, cfg["root_bone_length_m"] * S, 0.0)
        r.roll = 0.0
        hips = eb[HIPS]
        hips.use_connect = False
        hips.parent = r
        bpy.ops.object.mode_set(mode='OBJECT')
        d_arm.data.bones[ROOT].use_deform = True
        p("루트 본 추가: 본 %d개, 최상위=%s" %
          (len(d_arm.data.bones), [b.name for b in d_arm.data.bones if b.parent is None]))

    # 검증
    if base:
        assign_action(d_arm, new_actions[actions[0]])
        worst = 0.0
        for f in frames:
            scn.frame_set(f)
            nb = deformed_bbox(d_mesh)
            b = base[f]
            worst = max(worst, max(abs(nb[i] - b[i] * S) for i in range(6)))
        p("애니메이션 보존 검증: 최대 오차 %.6f m (%.4f mm) -> %s"
          % (worst, worst * 1000, "OK" if worst < 0.001 else "FAIL"))

    # 팔 각도 점검
    d_arm.data.pose_position = 'REST'
    bpy.context.view_layer.update()
    ra, rx = arm_angle(d_arm, "Left")
    d_arm.data.pose_position = 'POSE'
    p("레스트 왼팔 %.1f도 / X부호 %s" % (ra, "+" if rx > 0 else "-"))
    if actions:
        scn.frame_set(frames[len(frames) // 2])
        aa, ax = arm_angle(d_arm, "Left")
        ok = (ax > 0) == (rx > 0)
        p("애니 왼팔 %.1f도 / X부호 %s -> %s"
          % (aa, "+" if ax > 0 else "-", "OK" if ok else "FAIL: 리그/애니 출처 불일치 의심"))

    # ── 익스포트
    op = bpy.ops.export_scene.fbx
    valid = set(op.get_rna_type().properties.keys())

    COMMON = dict(
        use_selection=True,
        apply_scale_options='FBX_SCALE_NONE',
        axis_forward='-Z', axis_up='Y',
        use_space_transform=True, bake_space_transform=False,
        add_leaf_bones=False,
        primary_bone_axis='Y', secondary_bone_axis='X',
        use_armature_deform_only=False,
    )

    def do_export(path, objs, **kw):
        bpy.ops.object.mode_set(mode='OBJECT')
        bpy.ops.object.select_all(action='DESELECT')
        for o in objs:
            o.hide_set(False)
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        args = dict(COMMON)
        args["filepath"] = path
        args.update(kw)
        op(**{k: v for k, v in args.items() if k in valid})

    results = []

    # 스켈레탈 메시 — 반드시 REST, global_scale=1.0 (노드 스케일 100)
    if cfg["export_sk"]:
        sk_path = os.path.join(cfg["out_dir"], "%s_SK.fbx" % name)
        d_arm.data.pose_position = 'REST'
        scn.frame_set(1)
        do_export(sk_path, [d_arm, d_mesh],
                  object_types={'ARMATURE', 'MESH'},
                  bake_anim=False,
                  global_scale=1.0,
                  mesh_smooth_type='FACE',
                  use_mesh_modifiers=True,
                  path_mode='COPY', embed_textures=False)
        d_arm.data.pose_position = 'POSE'
        p("\n[SK] %s" % sk_path)
        results.append(fbx_report(sk_path))

    # 애니메이션 — POSE, ARMATURE 만, global_scale=0.01 (노드 스케일 1.0)
    for an in actions:
        assign_action(d_arm, new_actions[an])
        act = new_actions[an]
        scn.frame_start = int(act.frame_range[0])
        scn.frame_end = int(act.frame_range[1])
        scn.frame_set(scn.frame_start)
        safe = an.replace(" ", "_").replace("|", "_").replace(".", "_")
        ap = os.path.join(cfg["out_dir"], "%s_Anim_%s.fbx" % (name, safe))
        do_export(ap, [d_arm],
                  object_types={'ARMATURE'},
                  bake_anim=True,
                  bake_anim_use_all_bones=True,
                  bake_anim_use_nla_strips=False,
                  bake_anim_use_all_actions=False,
                  bake_anim_force_startend_keying=True,
                  bake_anim_step=1.0,
                  bake_anim_simplify_factor=0.0,
                  global_scale=0.01,
                  path_mode='AUTO')
        p("[ANIM] %s  (%d~%d)" % (ap, scn.frame_start, scn.frame_end))
        results.append(fbx_report(ap))

    # ── 리포트
    p("\n" + "=" * 72)
    p("검증 리포트")
    p("=" * 72)
    for r in results:
        is_anim = r["Geometry"] == 0
        p("\n%s (%.2f MB)" % (r["file"], r["size_mb"]))
        p("  UnitScaleFactor   %s" % r.get("UnitScaleFactor"))
        p("  노드 스케일(!=1)   %s" % (r["node_scaling"] or "없음"))
        p("  Geometry %d / Cluster %d / BindPose %d / LimbNode %d"
          % (r["Geometry"], r["Cluster"], r["BindPose"], r["LimbNode"]))
        p("  AnimationCurve %d / 스케일커브 %d개 최대 %.4f"
          % (r["AnimationCurve"], r["scale_curve_count"], r["scale_curve_max"]))
        p("  Smoothing %d / Normal %d" % (r["LayerElementSmoothing"], r["LayerElementNormal"]))

        checks = []
        if is_anim:
            checks.append(("Geometry == 0", r["Geometry"] == 0))
            checks.append(("Cluster == 0", r["Cluster"] == 0))
            checks.append(("스케일 커브 최대 == 1.0", r["scale_curve_max"] <= 1.01))
            checks.append(("노드 스케일 없음", not r["node_scaling"]))
        else:
            checks.append(("Geometry > 0", r["Geometry"] > 0))
            checks.append(("Cluster > 0", r["Cluster"] > 0))
            checks.append(("BindPose > 0", r["BindPose"] > 0))
            checks.append(("AnimationCurve == 0", r["AnimationCurve"] == 0))
            checks.append(("Smoothing > 0", r["LayerElementSmoothing"] > 0))
        for label, ok in checks:
            p("    [%s] %s" % ("OK" if ok else "FAIL", label))

    # 원본 복구
    if prev_action:
        assign_action(src_arm, prev_action)
    src_arm.data.pose_position = prev_pose
    scn.frame_set(prev_frame)
    for o in (d_arm, d_mesh):
        o.hide_set(True)
    p("\n원본 씬 상태 복구 완료 (.blend 는 저장하지 않았습니다)")
    return results


if __name__ == "__main__":
    prepare(CONFIG)
