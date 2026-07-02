#!/usr/bin/env python3
"""
Анимации (Idle, Walk, Jump, Cast) для рига HumanArmature (создаётся rig_human.py).

Использование:
    blender -b models/human_rigged.blend --python animate_human.py -- [выход.blend]

Если путь не указан — сохраняет поверх открытого файла.

Ноги/спина/голова/стопы используют local Z (после align_roll в rig_human.py
это мировая ось Y) для маха/наклона вперёд-назад — обычный Euler.

Кости рук (UpperArm.L/R) требуют композиции двух вращений, а не одной
Euler-оси:
  1) "опускание" из T-позы вдоль тела — вращение вокруг МИРОВОЙ оси X;
  2) "мах" вперёд-назад при ходьбе — вращение вокруг МИРОВОЙ оси Y,
     применяемое ПОВЕРХ уже опущенной руки.
Euler XYZ в Blender применяет углы вокруг ИСХОДНЫХ (rest) локальных
осей кости, а не последовательно вращающихся — поэтому одна Euler-тройка
не может выразить "опустить, а затем качнуть" для этой кости. Вместо
этого руки анимируются кватернионами: q_final = q_swing @ q_lower
(q_lower применяется первым). Из-за того, что бинд-поза правой руки
повёрнута на 180° относительно левой (T-поза в разные стороны),
ОДНО И ТО ЖЕ значение swing даёт естественный противофазный мах для
обеих рук — см. верификацию в истории задачи, разные знаки не нужны.

ForeArm/Hand — прямое продолжение родителя в бинд-позе (rest-relative =
identity), поэтому для них вращение вокруг "мировой" Y в СОБСТВЕННОМ
rest-фрейме кости работает как естественный шарнир (локоть/запястье)
относительно уже повёрнутого родителя — иерархия сама всё сложит.

Игра рисует персонажа от первого лица (см. mine.cpp) — камера стоит
почти вплотную к телу, поэтому руки в состоянии покоя держим НЕ
опущенными вдоль тела (тогда их не видно в кадре), а приподнятыми
перед собой (ARM_REST_*), как в большинстве игр от первого лица.

Idle и Walk зациклены (последний кадр повторяет первый). Jump и Cast —
разовые анимации.
"""
import sys
import math
import bpy
from mathutils import Quaternion

ARM_DOWN = math.radians(-90)  # полностью опущенная рука (используется только как крайний случай)
# Рука в покое приподнята и согнута в локте — так она остаётся в кадре от первого лица
ARM_REST_LOWER = math.radians(-42)
ARM_REST_ELBOW = math.radians(60)

ARM_BONES = ("UpperArm.L", "UpperArm.R", "ForeArm.L", "ForeArm.R", "Hand.L", "Hand.R")


def get_output_path():
    argv = sys.argv
    if "--" in argv:
        args = argv[argv.index("--") + 1:]
        if args:
            return args[0]
    return bpy.data.filepath


def set_rotation_modes(arm_obj):
    for pb in arm_obj.pose.bones:
        pb.rotation_mode = 'QUATERNION' if pb.name in ARM_BONES else 'XYZ'


def clear_pose(arm_obj):
    for pb in arm_obj.pose.bones:
        pb.rotation_euler = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)


def key_rot(pb, frame, x=None, y=None, z=None):
    rx, ry, rz = pb.rotation_euler
    if x is not None:
        rx = x
    if y is not None:
        ry = y
    if z is not None:
        rz = z
    pb.rotation_euler = (rx, ry, rz)
    pb.keyframe_insert(data_path="rotation_euler", frame=frame)


def key_loc(pb, frame, z=None):
    # Локальная Y кости Hips совпадает с мировой вертикальной Z (кость
    # направлена вверх, см. rig_human.py) — поэтому "мировой" вертикальный
    # сдвиг z пишется в pb.location.y, а не .z. Проверено численно.
    x, y, zz = pb.location
    if z is not None:
        y = z
    pb.location = (x, y, zz)
    pb.keyframe_insert(data_path="location", frame=frame)


def key_arm(pb, frame, lower=ARM_REST_LOWER, swing=0.0):
    q_lower = Quaternion((1.0, 0.0, 0.0), lower)
    q_swing = Quaternion((0.0, 1.0, 0.0), swing)
    pb.rotation_quaternion = q_swing @ q_lower
    pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def key_hinge(pb, frame, bend=0.0):
    # Общий шарнир для ForeArm (локоть) и Hand (запястье) — обе кости имеют
    # identity rest-relative к родителю, вращение вокруг локальной Y работает
    # как естественный шарнир относительно текущего (уже повёрнутого) родителя.
    pb.rotation_quaternion = Quaternion((0.0, 1.0, 0.0), bend)
    pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def new_action(arm_obj, name):
    if arm_obj.animation_data is None:
        arm_obj.animation_data_create()
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    arm_obj.animation_data.action = action
    return action


def build_idle(arm_obj):
    clear_pose(arm_obj)
    action = new_action(arm_obj, "Idle")
    pb = arm_obj.pose.bones

    CYCLE = 90  # 3 сек @30fps — медленное дыхание
    breathe_amp = math.radians(2)
    bob_amp = 0.012
    arm_sway_amp = math.radians(4)
    elbow_sway_amp = math.radians(6)
    wrist_sway_amp = math.radians(8)

    for f in range(0, CYCLE + 1, 15):
        phase = f / CYCLE
        s = math.sin(2 * math.pi * phase)

        key_rot(pb["Spine"], f, z=breathe_amp * s)
        key_rot(pb["Head"], f, z=breathe_amp * 0.5 * s)
        key_loc(pb["Hips"], f, z=bob_amp * s)

        key_arm(pb["UpperArm.L"], f, lower=ARM_REST_LOWER + arm_sway_amp * s)
        key_arm(pb["UpperArm.R"], f, lower=ARM_REST_LOWER + arm_sway_amp * s)
        key_hinge(pb["ForeArm.L"], f, bend=ARM_REST_ELBOW + elbow_sway_amp * s)
        key_hinge(pb["ForeArm.R"], f, bend=ARM_REST_ELBOW + elbow_sway_amp * s)
        key_hinge(pb["Hand.L"], f, bend=wrist_sway_amp * s)
        key_hinge(pb["Hand.R"], f, bend=wrist_sway_amp * s)

    return action


def build_walk(arm_obj):
    clear_pose(arm_obj)
    action = new_action(arm_obj, "Walk")
    pb = arm_obj.pose.bones

    CYCLE = 24  # 0.8 сек @30fps — цикл шага
    leg_swing = math.radians(30)
    knee_amp = math.radians(50)     # колено сгибается на "проходе" ноги через середину
    foot_amp = math.radians(18)     # стопа слегка довёртывается следом за голенью

    arm_swing = math.radians(35)
    elbow_amp = math.radians(25)    # локоть подгибается сильнее на замахе назад
    wrist_amp = math.radians(12)

    hip_bob_amp = 0.04
    spine_sway = math.radians(3)

    for f in range(0, CYCLE + 1, 6):
        phase = f / CYCLE
        s = math.sin(2 * math.pi * phase)
        c = math.cos(2 * math.pi * phase)

        key_rot(pb["UpperLeg.L"], f, z=leg_swing * s)
        key_rot(pb["UpperLeg.R"], f, z=-leg_swing * s)

        # Колено сгибается дважды за цикл (на каждом "проходе" бедра через
        # нейтральное положение) и распрямляется в крайних точках маха
        knee_l = knee_amp * max(0.0, -c)
        knee_r = knee_amp * max(0.0, c)
        key_rot(pb["LowerLeg.L"], f, z=knee_l)
        key_rot(pb["LowerLeg.R"], f, z=knee_r)

        # Стопа слегка "довзводится" вслед за голенью — носок подбирается,
        # когда колено согнуто (нога в воздухе), и распрямляется при опоре
        key_rot(pb["Foot.L"], f, z=-knee_l * (foot_amp / knee_amp) if knee_amp else 0.0)
        key_rot(pb["Foot.R"], f, z=-knee_r * (foot_amp / knee_amp) if knee_amp else 0.0)

        # Одно и то же значение swing для обеих рук даёт противофазный
        # мах — см. пояснение в докстринге модуля.
        arm_swing_val = -arm_swing * s
        key_arm(pb["UpperArm.L"], f, lower=ARM_REST_LOWER, swing=arm_swing_val)
        key_arm(pb["UpperArm.R"], f, lower=ARM_REST_LOWER, swing=arm_swing_val)

        # Локоть чуть сильнее подгибается на заднем ходе маха (естественная
        # инерция предплечья), запястье слегка пружинит следом
        elbow_val = ARM_REST_ELBOW + elbow_amp * max(0.0, s)
        key_hinge(pb["ForeArm.L"], f, bend=elbow_val)
        key_hinge(pb["ForeArm.R"], f, bend=elbow_val)
        key_hinge(pb["Hand.L"], f, bend=wrist_amp * s)
        key_hinge(pb["Hand.R"], f, bend=wrist_amp * s)

        key_rot(pb["Spine"], f, z=spine_sway * s)
        key_loc(pb["Hips"], f, z=hip_bob_amp * abs(s))

    return action


def build_jump(arm_obj):
    clear_pose(arm_obj)
    action = new_action(arm_obj, "Jump")
    pb = arm_obj.pose.bones

    crouch_knee = math.radians(35)
    tuck_knee = math.radians(55)
    foot_tuck = math.radians(20)
    arm_up = math.radians(75)
    elbow_pump = math.radians(35)

    # 0 покой -> 6 присед -> 12 толчок/взлёт -> 22 в воздухе (колени поджаты)
    # -> 30 приземление присед -> 36 покой
    # (knee, foot, arm_lower, elbow, hip_z)
    schedule = [
        (0,  0.0,          0.0,        ARM_REST_LOWER,                 ARM_REST_ELBOW,                 0.0),
        (6,  crouch_knee,  0.0,        ARM_REST_LOWER - arm_up * 0.3,  ARM_REST_ELBOW + elbow_pump,     -0.06),
        (12, 0.0,          0.0,        ARM_REST_LOWER - arm_up,        ARM_REST_ELBOW * 0.3,             0.05),
        (22, tuck_knee,    -foot_tuck, ARM_REST_LOWER - arm_up * 0.6,  ARM_REST_ELBOW + elbow_pump * 0.6, 0.10),
        (30, crouch_knee,  0.0,        ARM_REST_LOWER - arm_up * 0.3,  ARM_REST_ELBOW + elbow_pump,       -0.06),
        (36, 0.0,          0.0,        ARM_REST_LOWER,                 ARM_REST_ELBOW,                    0.0),
    ]

    for f, knee, foot, arm_lower, elbow, hip_z in schedule:
        key_rot(pb["LowerLeg.L"], f, z=knee)
        key_rot(pb["LowerLeg.R"], f, z=knee)
        key_rot(pb["Foot.L"], f, z=foot)
        key_rot(pb["Foot.R"], f, z=foot)
        key_arm(pb["UpperArm.L"], f, lower=arm_lower)
        key_arm(pb["UpperArm.R"], f, lower=arm_lower)
        key_hinge(pb["ForeArm.L"], f, bend=elbow)
        key_hinge(pb["ForeArm.R"], f, bend=elbow)
        key_loc(pb["Hips"], f, z=hip_z)

    return action


def build_cast(arm_obj):
    clear_pose(arm_obj)
    action = new_action(arm_obj, "Cast")
    pb = arm_obj.pose.bones

    # Замах назад -> резкий выброс руки вперёд-вверх (локоть распрямляется на "выстреле")
    # -> короткая задержка -> возврат. Правая рука кастует, всё остальное тела держим
    # в нейтральной стойке (ARM_REST_*) на всех кадрах — иначе непроанимированные
    # кости "прыгнут" в T-позу, когда этот Action проигрывается отдельно от Idle/Walk.
    windup_lower = ARM_REST_LOWER - math.radians(20)
    windup_swing = math.radians(-30)
    windup_elbow = math.radians(90)
    windup_wrist = math.radians(-15)

    thrust_lower = math.radians(-25)
    thrust_swing = math.radians(85)
    thrust_elbow = math.radians(10)
    thrust_wrist = math.radians(10)

    # (frame, lower, swing, elbow, wrist)
    schedule = [
        (0,  ARM_REST_LOWER, 0.0,          ARM_REST_ELBOW, 0.0),
        (3,  windup_lower,   windup_swing, windup_elbow,   windup_wrist),
        (7,  thrust_lower,   thrust_swing, thrust_elbow,   thrust_wrist),
        (11, thrust_lower,   thrust_swing, thrust_elbow,   thrust_wrist),
        (16, ARM_REST_LOWER, 0.0,          ARM_REST_ELBOW, 0.0),
    ]

    for f, lower, swing, elbow, wrist in schedule:
        key_arm(pb["UpperArm.R"], f, lower=lower, swing=swing)
        key_hinge(pb["ForeArm.R"], f, bend=elbow)
        key_hinge(pb["Hand.R"], f, bend=wrist)

        # Нейтральная стойка на тех же кадрах для остальных костей
        key_arm(pb["UpperArm.L"], f, lower=ARM_REST_LOWER)
        key_hinge(pb["ForeArm.L"], f, bend=ARM_REST_ELBOW)
        key_rot(pb["UpperLeg.L"], f, z=0.0)
        key_rot(pb["UpperLeg.R"], f, z=0.0)
        key_rot(pb["LowerLeg.L"], f, z=0.0)
        key_rot(pb["LowerLeg.R"], f, z=0.0)
        key_rot(pb["Spine"], f, z=0.0)
        key_rot(pb["Head"], f, z=0.0)
        key_loc(pb["Hips"], f, z=0.0)

    return action


if __name__ == "__main__":
    arm_obj = bpy.data.objects.get("HumanArmature")
    if arm_obj is None:
        print("Ошибка: объект HumanArmature не найден — сначала запустите rig_human.py")
        sys.exit(1)

    bpy.context.scene.render.fps = 30
    bpy.context.preferences.edit.keyframe_new_interpolation_type = 'LINEAR'

    set_rotation_modes(arm_obj)

    build_idle(arm_obj)
    build_walk(arm_obj)
    build_jump(arm_obj)
    build_cast(arm_obj)

    arm_obj.animation_data.action = bpy.data.actions["Idle"]

    output_path = get_output_path()
    bpy.ops.wm.save_as_mainfile(filepath=output_path)
    print(f"Анимации созданы (Idle, Walk, Jump, Cast), сохранено: {output_path}")
