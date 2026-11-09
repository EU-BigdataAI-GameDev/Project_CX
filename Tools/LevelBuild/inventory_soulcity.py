import unreal, json

ar = unreal.AssetRegistryHelpers.get_asset_registry()
flt = unreal.ARFilter(package_paths=["/Game/SoulCity"], class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")], recursive_paths=True)
assets = ar.get_assets(flt)
out = []
for a in assets:
    path = str(a.package_name)
    sm = unreal.load_asset(path)
    b = sm.get_bounding_box()
    mn, mx = b.min, b.max
    try:
        body = sm.get_editor_property("body_setup")
        coll = str(body.get_editor_property("collision_trace_flag")).split(".")[-1] if body else "none"
        agg = body.get_editor_property("agg_geom") if body else None
        prims = (len(agg.box_elems) + len(agg.convex_elems) + len(agg.sphyl_elems) + len(agg.sphere_elems)) if agg else 0
    except Exception as e:
        coll, prims = "err", -1
    mats = [m.get_editor_property("material_interface").get_name() if m.get_editor_property("material_interface") else None for m in sm.get_editor_property("static_materials")]
    out.append({
        "path": path,
        "min": [round(mn.x), round(mn.y), round(mn.z)],
        "max": [round(mx.x), round(mx.y), round(mx.z)],
        "size": [round(mx.x - mn.x), round(mx.y - mn.y), round(mx.z - mn.z)],
        "coll": coll, "prims": prims, "mats": mats,
        "nanite": sm.get_editor_property("nanite_settings").get_editor_property("enabled"),
    })
out_path = unreal.Paths.project_saved_dir() + "soul_inventory.json"
json.dump(out, open(out_path, "w"), indent=0)
print(len(out), "meshes ->", out_path)
