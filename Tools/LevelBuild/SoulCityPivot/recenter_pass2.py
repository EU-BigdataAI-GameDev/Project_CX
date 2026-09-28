import unreal, json
exec(open(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir() + "../Tools/LevelBuild/SoulCityPivot/recenter_lib.py"), encoding="utf-8").read())
pp = unreal.Paths.project_saved_dir() + "soulcity_pivot_plan.json"
data = json.load(open(pp))
for it in range(2):
    fixed = []
    for e in data["plan"]:
        sm = unreal.load_asset(e["path"])
        b = sm.get_bounding_box(); c = (b.min + b.max) * 0.5
        if c.length() < 1.0: continue
        recenter(sm, c)
        e["offset"] = [e["offset"][0] + c.x, e["offset"][1] + c.y, e["offset"][2] + c.z]
        b = sm.get_bounding_box(); c2 = (b.min + b.max) * 0.5
        unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
        fixed.append((e["path"].split("/")[-1], round(c.length(),1), round(c2.length(),1)))
    json.dump(data, open(pp, "w"), indent=1)
    print("iter", it, fixed)
