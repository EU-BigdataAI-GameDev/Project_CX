# 1단계: 재조정 대상/오프셋 계산 (자산 수정 없음)
import unreal, json
ar = unreal.AssetRegistryHelpers.get_asset_registry()
flt = unreal.ARFilter(package_paths=["/Game/SoulCity"], class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")], recursive_paths=True)
plan, skipped = [], []
for a in ar.get_assets(flt):
    p = str(a.package_name)
    nonmap = [str(r) for r in unreal.EditorAssetLibrary.find_package_referencers_for_asset(p, False)
              if not str(r).startswith("/Game/SoulCity/Maps") and not str(r).startswith("/Game/Project_CX/Maps")
              and not str(r).startswith("/Game/SoulCity/Environment/Materials")]
    sm = unreal.load_asset(p)
    b = sm.get_bounding_box(); c = (b.min + b.max) * 0.5
    if nonmap:
        skipped.append({"path": p, "reason": "used by " + ", ".join(nonmap)}); continue
    if c.length() < 1.0:
        skipped.append({"path": p, "reason": "already centered"}); continue
    plan.append({"path": p, "offset": [c.x, c.y, c.z]})
out = unreal.Paths.project_saved_dir() + "soulcity_pivot_plan.json"
json.dump({"plan": plan, "skipped": skipped}, open(out, "w"), indent=1)
print("to recenter:", len(plan), "skipped:", len(skipped))
for s in skipped:
    if s["reason"] != "already centered": print("  skip", s["path"].split("/")[-1], "-", s["reason"])
