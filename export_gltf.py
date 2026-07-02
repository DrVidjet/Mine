#!/usr/bin/env python3
"""
Экспорт рига с анимациями в glTF (.glb) для загрузки в raylib
(LoadModel + LoadModelAnimations — raylib не читает .blend/.fbx напрямую,
скелетная анимация поддерживается только через glTF/IQM).

Использование:
    blender -b models/human_rigged.blend --python export_gltf.py -- [выход.glb]

Если путь не указан — сохраняет рядом с исходным .blend, с тем же именем
и расширением .glb.

export_animation_mode='ACTIONS' — экспортирует КАЖДЫЙ Action, привязанный
к арматуре (Idle/Walk/Jump/Cast, все с use_fake_user=True), отдельным
клипом анимации в glTF, а не только текущий активный.
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
    return bpy.path.abspath(f"//{stem}.glb")


def export_gltf():
    output_path = get_output_path()

    bpy.ops.object.select_all(action='SELECT')

    bpy.ops.export_scene.gltf(
        filepath=output_path,
        export_format='GLB',
        use_selection=True,
        export_animations=True,
        export_animation_mode='ACTIONS',
        export_force_sampling=True,
        export_frame_range=False,
        export_skins=True,
        export_apply=True,
    )

    print(f"Экспортировано: {output_path}")


if __name__ == "__main__":
    export_gltf()
