#!/usr/bin/env python3
"""
Экспорт .blend модели в FBX для загрузки на Mixamo (авто-риггинг).

Использование:
    blender -b <модель.blend> --python export_fbx.py -- [выход.fbx]

Если путь для выхода не указан — сохраняет рядом с исходным .blend,
с тем же именем и расширением .fbx.

Пример:
    blender -b models/human.blend --python export_fbx.py -- models/human.fbx

Скрипт:
  - выделяет все mesh-объекты сцены (свет и камеру не трогает)
  - применяет вращение и масштаб (transform_apply), чтобы в FBX
    не было "грязного" transform — иначе Mixamo и другие DCC
    криво считают привязку скелета
  - экспортирует в FBX с осями, которые ожидает Mixamo (Forward -Z, Up Y)

Исходный .blend не изменяется и не сохраняется.
"""
import sys
import bpy


def get_output_path():
    argv = sys.argv
    if "--" in argv:
        args = argv[argv.index("--") + 1:]
        if args:
            return args[0]
    stem = bpy.path.basename(bpy.data.filepath).rsplit(".", 1)[0]
    return bpy.path.abspath(f"//{stem}.fbx")


def export_fbx():
    output_path = get_output_path()

    mesh_objs = [o for o in bpy.data.objects if o.type == 'MESH']
    if not mesh_objs:
        print("Ошибка: в сцене нет mesh-объектов")
        sys.exit(1)

    bpy.ops.object.select_all(action='DESELECT')
    for obj in mesh_objs:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_objs[0]

    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    bpy.ops.export_scene.fbx(
        filepath=output_path,
        use_selection=True,
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL',
        axis_forward='-Z',
        axis_up='Y',
        object_types={'MESH'},
        mesh_smooth_type='FACE',
        add_leaf_bones=False,
        bake_anim=False,
    )

    print(f"Экспортировано: {output_path}  ({len(mesh_objs)} mesh-объектов)")


if __name__ == "__main__":
    export_fbx()
