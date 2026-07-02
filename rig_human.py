#!/usr/bin/env python3
"""
Сборка скелета для блочной human-модели (15 отдельных кубов) и жёсткая
привязка каждого куба к своей кости (вес 1.0, без смешивания — как в
блочных Minecraft-риггах). Работает без Mixamo, полностью локально.

Использование:
    blender -b models/human.blend --python rig_human.py -- [выход.blend]

Если путь для выхода не указан — сохраняет models/human_rigged.blend.
Исходный human.blend не перезаписывается.

Модель ожидается в T-позе (руки вытянуты в стороны по оси Y). Любая
существующая арматура в сцене удаляется и пересоздаётся с нуля.

Сопоставление кубов частям тела (ROLE_MAP) определено вручную по итогам
осмотра геометрии — имена кубов в файле не несут семантики ("Куб", "Куб.001", ...).
"""
import sys
import bpy
from mathutils import Vector

ROLE_MAP = {
    "Куб.005": "torso",
    "Куб.009": "head_base",
    "Куб.012": "head_top",
    "Куб.001": "upperarm_L",
    "Куб.003": "forearm_L",
    "Куб.014": "hand_L",
    "Куб.011": "upperarm_R",
    "Куб.010": "forearm_R",
    "Куб.013": "hand_R",
    "Куб.006": "upperleg_L",
    "Куб":     "lowerleg_L",
    "Куб.002": "foot_L",
    "Куб.007": "upperleg_R",
    "Куб.008": "lowerleg_R",
    "Куб.004": "foot_R",
}

BONE_FOR_ROLE = {
    "torso": "Spine",
    "head_base": "Head",
    "head_top": "Head",
    "upperarm_L": "UpperArm.L", "forearm_L": "ForeArm.L", "hand_L": "Hand.L",
    "upperarm_R": "UpperArm.R", "forearm_R": "ForeArm.R", "hand_R": "Hand.R",
    "upperleg_L": "UpperLeg.L", "lowerleg_L": "LowerLeg.L", "foot_L": "Foot.L",
    "upperleg_R": "UpperLeg.R", "lowerleg_R": "LowerLeg.R", "foot_R": "Foot.R",
}


def get_output_path():
    argv = sys.argv
    if "--" in argv:
        args = argv[argv.index("--") + 1:]
        if args:
            return args[0]
    return bpy.path.abspath("//human_rigged.blend")


def world_bbox(obj):
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    xs = [c.x for c in corners]
    ys = [c.y for c in corners]
    zs = [c.z for c in corners]
    return (min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs))


def center(bbox):
    (x0, x1), (y0, y1), (z0, z1) = bbox
    return (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2


def top_z(bbox):
    return bbox[2][1]


def bottom_z(bbox):
    return bbox[2][0]


def union_bbox(a, b):
    (ax0, ax1), (ay0, ay1), (az0, az1) = a
    (bx0, bx1), (by0, by1), (bz0, bz1) = b
    return (min(ax0, bx0), max(ax1, bx1)), (min(ay0, by0), max(ay1, by1)), (min(az0, bz0), max(az1, bz1))


def remove_existing_armatures():
    for obj in list(bpy.data.objects):
        if obj.type == 'ARMATURE':
            for mesh_obj in bpy.data.objects:
                if mesh_obj.type == 'MESH':
                    for mod in list(mesh_obj.modifiers):
                        if mod.type == 'ARMATURE' and mod.object == obj:
                            mesh_obj.modifiers.remove(mod)
                    if mesh_obj.parent == obj:
                        mesh_obj.parent = None
            bpy.data.objects.remove(obj, do_unlink=True)


def build_rig():
    remove_existing_armatures()

    objs = {}
    for name, role in ROLE_MAP.items():
        obj = bpy.data.objects.get(name)
        if obj is None:
            print(f"Ошибка: объект не найден в сцене: {name}")
            sys.exit(1)
        objs[role] = obj

    bboxes = {role: world_bbox(obj) for role, obj in objs.items()}
    bboxes["head"] = union_bbox(bboxes["head_base"], bboxes["head_top"])

    torso_bbox = bboxes["torso"]
    tx, ty, _ = center(torso_bbox)

    arm_data = bpy.data.armatures.new("HumanArmature")
    arm_obj = bpy.data.objects.new("HumanArmature", arm_data)
    bpy.context.scene.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')
    ebones = arm_data.edit_bones

    # Roll фиксируется явно, чтобы локальные оси костей были предсказуемы
    # для последующей анимации (а не зависели от авто-roll Blender):
    #   вертикальные кости (спина/голова/ноги/стопы) -> local Z = мировой Y,
    #     вращение вокруг local Z = мах вперёд-назад (сгибание в плоскости XZ)
    #   кости рук (изначально вдоль мировой Y, T-поза) -> local Z = мировой Z,
    #     вращение вокруг local X = опускание/мах рукой вперёд-назад
    VERTICAL_ROLL = Vector((0.0, 1.0, 0.0))
    ARM_ROLL = Vector((0.0, 0.0, 1.0))

    def make_vertical_bone(name, bbox, parent):
        x, y, _ = center(bbox)
        b = ebones.new(name)
        b.head = (x, y, top_z(bbox))
        b.tail = (x, y, bottom_z(bbox))
        b.parent = parent
        b.align_roll(VERTICAL_ROLL)
        return b

    def make_arm_bone(name, bbox, parent, outward_sign):
        x, _, z = center(bbox)
        (y0, y1) = bbox[1]
        if outward_sign > 0:
            head_y, tail_y = y0, y1
        else:
            head_y, tail_y = y1, y0
        b = ebones.new(name)
        b.head = (x, head_y, z)
        b.tail = (x, tail_y, z)
        b.parent = parent
        b.align_roll(ARM_ROLL)
        return b

    hips = ebones.new("Hips")
    hips.head = (tx, ty, bottom_z(torso_bbox))
    hips.tail = (tx, ty, bottom_z(torso_bbox) + 0.1)
    hips.align_roll(VERTICAL_ROLL)

    spine = ebones.new("Spine")
    spine.head = (tx, ty, bottom_z(torso_bbox))
    spine.tail = (tx, ty, top_z(torso_bbox))
    spine.parent = hips
    spine.align_roll(VERTICAL_ROLL)

    hx, hy, _ = center(bboxes["head"])
    head = ebones.new("Head")
    head.head = (hx, hy, bottom_z(bboxes["head"]))
    head.tail = (hx, hy, top_z(bboxes["head"]))
    head.parent = spine
    head.align_roll(VERTICAL_ROLL)

    for side, sign in (("L", 1), ("R", -1)):
        upperarm = make_arm_bone(f"UpperArm.{side}", bboxes[f"upperarm_{side}"], spine, sign)
        forearm = make_arm_bone(f"ForeArm.{side}", bboxes[f"forearm_{side}"], upperarm, sign)
        make_arm_bone(f"Hand.{side}", bboxes[f"hand_{side}"], forearm, sign)

        upperleg = make_vertical_bone(f"UpperLeg.{side}", bboxes[f"upperleg_{side}"], hips)
        lowerleg = make_vertical_bone(f"LowerLeg.{side}", bboxes[f"lowerleg_{side}"], upperleg)

        (fx0, fx1), (fy0, fy1), (fz0, fz1) = bboxes[f"foot_{side}"]
        fy = (fy0 + fy1) / 2
        fz = (fz0 + fz1) / 2
        foot = ebones.new(f"Foot.{side}")
        foot.head = (fx0, fy, fz)
        foot.tail = (fx1, fy, fz)
        foot.parent = lowerleg
        foot.align_roll(VERTICAL_ROLL)

    bpy.ops.object.mode_set(mode='OBJECT')

    for role, obj in objs.items():
        bone_name = "Head" if role in ("head_base", "head_top") else BONE_FOR_ROLE[role]

        vg = obj.vertex_groups.new(name=bone_name)
        vg.add(range(len(obj.data.vertices)), 1.0, 'REPLACE')

        mod = obj.modifiers.new(name="Armature", type='ARMATURE')
        mod.object = arm_obj

        obj.parent = arm_obj
        obj.matrix_parent_inverse = arm_obj.matrix_world.inverted()

    return arm_obj


if __name__ == "__main__":
    build_rig()
    output_path = get_output_path()
    bpy.ops.wm.save_as_mainfile(filepath=output_path)
    print(f"Скелет собран, файл сохранён: {output_path}")
