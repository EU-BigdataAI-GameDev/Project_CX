"""
거미로봇 공격 애니메이션(AS_SpiderBot_Attack)의 saw_blade 회전 축 수정 — 에디터 원격 실행.

원인(2026-11-04 측정):
- 톱날 원판(Mat_SawGray 정점 294개, 전부 saw_blade 가중치 1.0)을 glTF로 내보내 PCA: 원판 두께 방향(법선) = 메시 +X,
  원판 면 = 메시 Y-Z 평면(팔 방향 Y를 포함하는 세로면).
- saw_blade 참조 회전 = pitch -90 → 본 로컬 축(메시 공간): X = 아래(-Z), Y = +Y(팔 방향), **Z = +X = 원판 법선**.
- Idle(SpiderBot_Skeletal_Anim)은 본 로컬 -Z 축으로 돈다(약 184°/s) → **올바름(수정하지 않음)**.
- Attack은 Rotator(pitch=-90+θ) 키 = 부모 Y(팔 방향, 원판 면 안의 축)로 돌아서 톱날이 옆으로 굴렀다(공중제비).
수정: saw_blade 트랙만 다시 쓴다 — q(f) = q_ref * Rot(local Z, -θ), θ = 1080° * f / 24(루프당 3바퀴, Idle과 같은 방향).
       프레임당 45°(< 180°)라 보간이 뒤집히지 않는다. 다른 본 트랙은 그대로.
"""
import unreal, math

ATTACK = "/Game/Project_CX/Characters/Enemy/SpiderBot/AS_SpiderBot_Attack"
TURNS = 3.0


def qmul(a, b):
    # 해밀턴 곱 a*b (UE FQuat 연산자와 같은 의미: b를 먼저, 그다음 a)
    return unreal.Quat(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z)


def _main():
    eal = unreal.EditorAssetLibrary
    anim = eal.load_asset(ATTACK)
    c = anim.controller
    frames = int(round(anim.get_play_length() * 24.0))
    n = frames + 1
    opts = unreal.AnimPoseEvaluationOptions()
    pos, rot, scl = [], [], []
    q_ref = unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0).quaternion()
    for f in range(n):
        p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, f / 24.0, opts)
        t = unreal.AnimPoseExtensions.get_bone_pose(p, "saw_blade", unreal.AnimPoseSpaces.LOCAL)
        pos.append(t.translation)
        scl.append(t.scale3d)
        th = math.radians(-360.0 * TURNS * f / frames)
        qz = unreal.Quat(0.0, 0.0, math.sin(th / 2), math.cos(th / 2))
        rot.append(qmul(q_ref, qz))
    c.open_bracket("SpiderBot saw blade spin axis")
    c.set_bone_track_keys("saw_blade", pos, rot, scl)
    c.close_bracket()
    # 확인: 프레임 0 기준 상대 회전 축(본 로컬)
    p0 = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 0.0, opts)
    q0 = unreal.AnimPoseExtensions.get_bone_pose(p0, "saw_blade", unreal.AnimPoseSpaces.LOCAL).rotation
    for f in (1, 2, 4, 12):
        p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, f / 24.0, opts)
        q = unreal.AnimPoseExtensions.get_bone_pose(p, "saw_blade", unreal.AnimPoseSpaces.LOCAL).rotation
        d = q0.inversed() * q
        ax = d.get_rotation_axis()
        print("frame", f, "rel axis (bone local)", round(ax.x, 3), round(ax.y, 3), round(ax.z, 3), "angle", round(math.degrees(d.get_angle()), 1))
    eal.save_loaded_asset(anim, False)
    print("saved", anim.get_path_name(), "frames", frames)


_main()
del _main
