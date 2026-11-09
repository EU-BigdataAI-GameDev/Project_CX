"""
플레이어 카메라에 절대 보이지 않는 높이의 배경 메시를 찾는다(빌더/수정 스크립트 공용).

카메라 모델(BP 실측):
  여동생/오빠 공통(오빠 BP가 기준): FOV 37.5, 카메라 = 캐릭터 앞 1289, 발 위 433, 피치 -10 (PIE 실측)
  수평 FOV 고정(UE 기본 MaintainXFOV). 위아래 화면 분할(뷰포트 1920x540, 약 3.56:1) → 수직 반각 5.3°
  (1인 16:9 전체 화면이면 10.7°, 좌우 분할이면 20.8° — ASPECT 값을 바꿔서 쓴다)
캐릭터 위치: 레인 앞쪽 끝(X = LANE_FRONT)에서 실제 발판 높이 + 점프 90cm 까지 (발판은 COL 박스에 라인트레이스)
→ 어떤 캐릭터 위치에서도 중심이 시야 위쪽 경계보다 높은 메시 = 화면 위로 과하게 솟은 배치로 보고 제거.
"""
import unreal, math

ASPECT = 1920.0 / 540.0   # 화면 분할 한 칸
FOV = 37.5
V_HALF = math.degrees(math.atan(math.tan(math.radians(FOV / 2)) / ASPECT))   # 5.3°
CAMS = [  # (캐릭터 앞 거리, 발 위 높이, 피치)
    (1289.0, 433.0, -10.0),   # 여동생 = 오빠 (오빠 BP 기준, PIE 실측)
]

def walk_profile(world, y0, y1, step=100.0, x=0.0):
    """y별 가장 높은 발판 높이(COL 박스만 충돌이 있으므로 라인트레이스로 얻는다)."""
    prof = []
    y = y0
    while y <= y1:
        h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 5000), unreal.Vector(x, y, -1000),
                                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if h:
            t = h.to_tuple()
            if not t[9].get_actor_label().startswith("COL_Bound"):
                prof.append((y, t[4].z))
        y += step
    return prof

def visible_top(prof, lane_front, obj_x_min, obj_y0, obj_y1, jump=90.0):
    """이 물체(가장 가까운 X, Y 범위)가 보일 수 있는 최대 높이. 한 번도 화면에 안 들어오면 -inf."""
    best = -1e9
    for (yc, zc) in prof:
        for back, up, pitch in CAMS:
            cam_x = lane_front - back
            d = obj_x_min - cam_x
            if d <= 0:
                continue
            half_w = d * math.tan(math.radians(FOV / 2))
            if obj_y1 < yc - half_w or obj_y0 > yc + half_w:
                continue
            top = zc + jump + up + d * math.tan(math.radians(pitch + V_HALF))
            best = max(best, top)
    return best

def find_hidden(world, actors, lane_front, y0, y1):
    """actors 중 중심이 시야 위쪽 경계보다 높은 것(= 어떤 위치에서도 절반 이상이 화면 밖 위쪽)."""
    prof = walk_profile(world, y0, y1)
    hidden = []
    for a in actors:
        o, e = a.get_actor_bounds(False)
        if visible_top(prof, lane_front, o.x - e.x, o.y - e.y, o.y + e.y) < o.z:
            hidden.append(a)
    return hidden
