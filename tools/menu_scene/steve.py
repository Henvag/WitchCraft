"""Software renderer for a Minecraft player model in a seated pose.

Renders the classic 64x64 skin layout (both layers) as textured boxes with a
z-buffer and simple two-light shading, so a skin can be composited into a
pre-rendered video without a GPU or game client. Units are skin pixels
(1/16 block); the character faces +Z with +Y up and its left side at +X.
"""
import math

import numpy as np

# Box texture origins (u, v) and sizes (w, h, d) for the 64x64 layout.
PARTS = {
    'head': ((0, 0), (32, 0), (8, 8, 8)),
    'body': ((16, 16), (16, 32), (8, 12, 4)),
    'right_arm': ((40, 16), (40, 32), (4, 12, 4)),
    'left_arm': ((32, 48), (48, 48), (4, 12, 4)),
    'right_leg': ((0, 16), (0, 32), (4, 12, 4)),
    'left_leg': ((16, 48), (0, 48), (4, 12, 4)),
}
SLIM_ARM = 3


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def box_faces(origin, size, uv, inflate=0.0):
    """Returns (corners[4] in TL, TR, BR, BL texture order, texture rect, normal)
    for each face of a box whose min corner is origin."""
    x0, y0, z0 = origin
    w, h, d = size
    u, v = uv
    x0, y0, z0 = x0 - inflate, y0 - inflate, z0 - inflate
    x1, y1, z1 = x0 + w + 2 * inflate, y0 + h + 2 * inflate, z0 + d + 2 * inflate
    P = lambda x, y, z: np.array([x, y, z], float)
    return [
        # front (+Z): texture left is the character's right (-X)
        ((P(x0, y1, z1), P(x1, y1, z1), P(x1, y0, z1), P(x0, y0, z1)), (u + d, v + d, w, h), (0, 0, 1)),
        # back (-Z)
        ((P(x1, y1, z0), P(x0, y1, z0), P(x0, y0, z0), P(x1, y0, z0)), (u + 2 * d + w, v + d, w, h), (0, 0, -1)),
        # right (-X): texture left is the back
        ((P(x0, y1, z0), P(x0, y1, z1), P(x0, y0, z1), P(x0, y0, z0)), (u, v + d, d, h), (-1, 0, 0)),
        # left (+X): texture left is the front
        ((P(x1, y1, z1), P(x1, y1, z0), P(x1, y0, z0), P(x1, y0, z1)), (u + d + w, v + d, d, h), (1, 0, 0)),
        # top (+Y): texture bottom edge meets the front face
        ((P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)), (u + d, v, w, d), (0, 1, 0)),
        # bottom (-Y)
        ((P(x0, y0, z1), P(x1, y0, z1), P(x1, y0, z0), P(x0, y0, z0)), (u + d + w, v, w, d), (0, -1, 0)),
    ]


class Pose:
    """Joint angles in radians. Defaults sit on the ground against a wall,
    head bowed and forearms resting on the thighs, like Geralt in the menu."""

    def __init__(self, breath=0.0):
        self.body_pitch = -0.10 - 0.015 * breath   # leaning back
        self.head_pitch = 0.42 + 0.03 * breath     # looking down
        self.head_yaw = -0.18
        self.right_arm = (-0.75 - 0.02 * breath, 0.10, 0.12)  # pitch, yaw, roll
        self.left_arm = (-0.95 - 0.02 * breath, -0.25, -0.08)
        self.right_leg = (-1.50, 0.10)  # pitch (forward), yaw (splay)
        self.left_leg = (-1.15, -0.22)
        self.breath = breath


def build(pose, slim=False):
    """Returns world-space quads: (corners, texture rect, normal, layer, part)."""
    arm_w = SLIM_ARM if slim else 4
    hip_y = 2.0
    body_rot = rot_x(pose.body_pitch)
    neck = np.array([0, hip_y + 12, 0])
    shoulder_y = hip_y + 10
    parts = []

    def add(name, origin, size, rotation, pivot, base_offset=np.zeros(3)):
        uv, uv2, _ = PARTS[name]
        for layer, tex, inflate in ((0, uv, 0.0), (1, uv2, 0.5 if name == 'head' else 0.25)):
            for corners, rect, normal in box_faces(origin, size, tex, inflate):
                world = [rotation @ (c - pivot) + pivot + base_offset for c in corners]
                parts.append((world, rect, rotation @ np.array(normal, float), layer, name))

    hip = np.array([0, hip_y, 0], float)
    add('body', (-4, hip_y, -2), (8, 12, 4), body_rot, hip)
    head_rot = body_rot @ rot_y(pose.head_yaw) @ rot_x(pose.head_pitch)
    neck_world = body_rot @ (neck - hip) + hip
    add('head', (-4, 0, -4), (8, 8, 8), head_rot, np.zeros(3), neck_world)

    for name, side, (pitch, yaw, roll) in (('right_arm', -1, pose.right_arm),
                                            ('left_arm', 1, pose.left_arm)):
        shoulder = np.array([side * (4 + arm_w / 2.0), shoulder_y, 0], float)
        shoulder_world = body_rot @ (shoulder - hip) + hip
        rot = body_rot @ rot_y(yaw) @ rot_x(pitch) @ rot_z(roll * side)
        x0 = -arm_w / 2.0
        add(name, (x0, -10, -2), (arm_w, 12, 4), rot, np.zeros(3), shoulder_world)

    for name, side, (pitch, yaw) in (('right_leg', -1, pose.right_leg),
                                      ('left_leg', 1, pose.left_leg)):
        joint = np.array([side * 2.0, hip_y, 0], float)
        rot = rot_y(yaw) @ rot_x(pitch)
        add(name, (-2, -12, -2), (4, 12, 4), rot, np.zeros(3), joint)
    return parts


def render(skin, pose, view, out_size, scale, offset, lights, ambient, slim=False, ss=2):
    """Rasterises the model. view: 3x3 camera rotation (model to camera),
    camera looks down -Z. Projection is orthographic: pixel = offset +
    scale * (x, -y). Returns premultiplied RGB float image and alpha."""
    W, H = out_size
    W2, H2 = W * ss, H * ss
    color = np.zeros((H2, W2, 3), np.float32)
    alpha = np.zeros((H2, W2), np.float32)
    depth = np.full((H2, W2), -np.inf, np.float32)
    tex = skin.astype(np.float32) / 255.0
    quads = build(pose, slim)
    # Base layer first, then the overlay so equal depths favour the overlay.
    quads.sort(key=lambda q: q[3])
    for corners, (tu, tv, tw, th), normal, layer, _ in quads:
        n = view @ normal
        if n[2] <= 0:  # back-facing
            continue
        cam = [view @ c for c in corners]
        pts = [(offset[0] * ss + scale * ss * c[0], offset[1] * ss - scale * ss * c[1], c[2]) for c in cam]
        uvs = [(tu, tv), (tu + tw, tv), (tu + tw, tv + th), (tu, tv + th)]
        shade = ambient.copy()
        for direction, light_color in lights:
            shade += light_color * max(0.0, float(np.dot(n, direction)))
        for tri in ((0, 1, 2), (0, 2, 3)):
            _raster(color, alpha, depth, [pts[i] for i in tri], [uvs[i] for i in tri], tex, shade, layer)
    if ss > 1:
        color = color.reshape(H, ss, W, ss, 3).mean(axis=(1, 3))
        alpha = alpha.reshape(H, ss, W, ss).mean(axis=(1, 3))
    return color, alpha


def _raster(color, alpha, depth, pts, uvs, tex, shade, layer):
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts
    minx, maxx = int(max(0, math.floor(min(x0, x1, x2)))), int(min(color.shape[1] - 1, math.ceil(max(x0, x1, x2))))
    miny, maxy = int(max(0, math.floor(min(y0, y1, y2)))), int(min(color.shape[0] - 1, math.ceil(max(y0, y1, y2))))
    if minx > maxx or miny > maxy:
        return
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if abs(area) < 1e-9:
        return
    ys, xs = np.mgrid[miny:maxy + 1, minx:maxx + 1].astype(np.float32)
    xs += 0.5
    ys += 0.5
    w0 = ((x1 - xs) * (y2 - ys) - (x2 - xs) * (y1 - ys)) / area
    w1 = ((x2 - xs) * (y0 - ys) - (x0 - xs) * (y2 - ys)) / area
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
    if not inside.any():
        return
    z = w0 * z0 + w1 * z1 + w2 * z2
    u = w0 * uvs[0][0] + w1 * uvs[1][0] + w2 * uvs[2][0]
    v = w0 * uvs[0][1] + w1 * uvs[1][1] + w2 * uvs[2][1]
    ui = np.clip(np.floor(u), 0, tex.shape[1] - 1).astype(int)
    vi = np.clip(np.floor(v), 0, tex.shape[0] - 1).astype(int)
    texel = tex[vi, ui]
    region_depth = depth[miny:maxy + 1, minx:maxx + 1]
    bias = 0.01 if layer else 0.0
    visible = inside & (texel[..., 3] > 0.5) & (z + bias > region_depth)
    if not visible.any():
        return
    region_depth[visible] = z[visible] + bias
    rgb = texel[..., :3] * shade
    region = color[miny:maxy + 1, minx:maxx + 1]
    region[visible] = rgb[visible]
    alpha[miny:maxy + 1, minx:maxx + 1][visible] = 1.0
