"""
Soul City 메시 피벗 중심화 도구 (2026-09-28 실행 완료 — 다시 실행할 필요 없음)

순서(에디터 Python 원격 실행):
  1. recenter_plan.py   : 대상/오프셋 계산 → Saved/soulcity_pivot_plan.json (자산 수정 없음)
  2. recenter_apply.py  : 앞에 RANGE=(a,b) 를 붙여 배치 실행. 모든 LOD 정점 + 단순 콜리전 이동 후 저장
  3. recenter_pass2.py  : 빌드 후 바운드가 다시 계산되며 남는 잔차 보정(오프셋 누적 기록)
  4. compensate.py      : 앞에 MAP='...' 을 붙여 실행. 해당 맵에서 이 메시를 쓰는 액터/인스턴스 위치를 보정
결과 오프셋: Docs/LevelManifests/SoulCity_pivot_offsets.json
파티클/블루프린트가 참조하는 메시는 이펙트가 깨지지 않도록 제외했다.
"""
import unreal
GS = unreal.GeometryScript_AssetUtils
sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

def stats(sm):
    return dict(lods=sme.get_lod_count(sm), verts=[sme.get_number_verts(sm, i) for i in range(sme.get_lod_count(sm))],
                tris=[sme.get_number_triangles(sm, i) if hasattr(sme,"get_number_triangles") else -1 for i in range(sme.get_lod_count(sm))],
                uvs=[sme.get_num_uv_channels(sm, i) for i in range(sme.get_lod_count(sm))],
                mats=len(sm.static_materials), coll=sme.get_simple_collision_count(sm),
                bb=(sm.get_bounding_box().min, sm.get_bounding_box().max),
                lmres=sm.get_editor_property("light_map_resolution"), lmidx=sm.get_editor_property("light_map_coordinate_index"))

def recenter(sm, c):
    """메시 정점을 -c 만큼 옮겨 피벗을 c(원래 좌표계)로 이동. 모든 LOD + 단순 콜리전 포함."""
    n = sme.get_lod_count(sm)
    col = None
    if sme.get_simple_collision_count(sm) > 0:
        col = unreal.GeometryScript_Collision.get_simple_collision_from_static_mesh(sm)
    for i in range(n):
        dm = unreal.DynamicMesh()
        ropt = unreal.GeometryScriptCopyMeshFromAssetOptions(apply_build_settings=False, request_tangents=False, ignore_remove_degenerates=True)
        rlod = unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.SOURCE_MODEL, lod_index=i)
        dm, outcome = GS.copy_mesh_from_static_mesh_v2(sm, dm, ropt, rlod)
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("read fail lod %d" % i)
        unreal.GeometryScript_MeshTransforms.translate_mesh(dm, unreal.Vector(-c.x, -c.y, -c.z))
        wopt = unreal.GeometryScriptCopyMeshToAssetOptions(enable_recompute_normals=False, enable_recompute_tangents=False,
                    enable_remove_degenerates=False, replace_materials=False, apply_nanite_settings=False, emit_transaction=False,
                    defer_mesh_post_edit_change=(i < n - 1))
        wlod = unreal.GeometryScriptMeshWriteLOD(write_hi_res_source=False, lod_index=i)
        out = GS.copy_mesh_to_static_mesh(dm, sm, wopt, wlod)
        outcome = out[-1] if isinstance(out, tuple) else out
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("write fail lod %d" % i)
    if col is not None:
        col, ok = unreal.GeometryScript_Collision.transform_simple_collision_shapes(col, unreal.Transform(location=unreal.Vector(-c.x, -c.y, -c.z)), unreal.GeometryScriptTransformCollisionOptions())
        unreal.GeometryScript_Collision.set_simple_collision_of_static_mesh(col, sm, unreal.GeometryScriptSetSimpleCollisionOptions())
