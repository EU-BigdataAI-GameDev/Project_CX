# 2단계: 계획대로 메시 정점/콜리전 이동 후 저장. RANGE=(a,b) 를 앞에 붙여 배치 실행
import unreal, json, time
exec(open(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Tools/LevelBuild/SoulCityPivot/recenter_lib.py"), encoding="utf-8").read())
plan = json.load(open(unreal.Paths.project_saved_dir() + "soulcity_pivot_plan.json"))["plan"]
t0 = time.time(); fails = []
for e in plan[RANGE[0]:RANGE[1]]:
    sm = unreal.load_asset(e["path"])
    b = sm.get_bounding_box(); c = (b.min + b.max) * 0.5
    if c.length() < 1.0:
        continue  # 이미 처리됨(재실행 안전)
    try:
        recenter(sm, unreal.Vector(*e["offset"]))
        b = sm.get_bounding_box(); c2 = (b.min + b.max) * 0.5
        if c2.length() > 1.0: fails.append((e["path"], "residual %.1f" % c2.length()))
        unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
    except Exception as ex:
        fails.append((e["path"], str(ex)))
print("batch", RANGE, "done in %.0fs" % (time.time() - t0), "fails:", fails)
