"""Exemplar-based hole filling (Criminisi-style, without the data term).

Repeatedly picks the hole-front patch with the most known pixels and copies
the missing pixels from the best-matching fully known patch nearby, found
with cv2.matchTemplate. Good for continuing foliage, stone and ground texture
behind a removed figure, where diffusion inpainting only smears.
"""
import cv2
import numpy as np


def exemplar_inpaint(img, hole, forbid, patch=11, window=260):
    """img: HxWx3 uint8. hole: bool mask to fill. forbid: bool mask of pixels
    that must not be copied (for example the removed figure itself)."""
    H, W = hole.shape
    r = patch // 2
    known = (~hole).astype(np.uint8)
    source_ok = cv2.erode(((~hole) & (~forbid)).astype(np.uint8), np.ones((patch, patch), np.uint8))
    ys, xs = np.nonzero(hole)
    if not len(ys):
        return img.copy()
    by0, by1 = max(ys.min() - r, 0), min(ys.max() + r + 1, H)
    bx0, bx1 = max(xs.min() - r, 0), min(xs.max() + r + 1, W)
    out = img.astype(np.float32)
    while True:
        roi = known[by0:by1, bx0:bx1]
        if roi.all():
            break
        fraction = cv2.boxFilter(roi.astype(np.float32), -1, (patch, patch),
                                 borderType=cv2.BORDER_CONSTANT)
        fraction[roi > 0] = -1
        py, px = np.unravel_index(np.argmax(fraction), fraction.shape)
        y0 = min(max(py + by0 - r, 0), H - patch)
        x0 = min(max(px + bx0 - r, 0), W - patch)
        target = out[y0:y0 + patch, x0:x0 + patch]
        weights = np.repeat(known[y0:y0 + patch, x0:x0 + patch, None], 3, axis=2).astype(np.float32)
        search = window
        while True:
            wy0, wy1 = max(y0 - search, 0), min(y0 + patch + search, H)
            wx0, wx1 = max(x0 - search, 0), min(x0 + patch + search, W)
            cost = cv2.matchTemplate(out[wy0:wy1, wx0:wx1], target, cv2.TM_SQDIFF, mask=weights)
            valid = source_ok[wy0 + r:wy0 + r + cost.shape[0], wx0 + r:wx0 + r + cost.shape[1]] > 0
            cost = np.where(valid & np.isfinite(cost), cost, np.inf)
            if np.isfinite(cost).any() or search >= max(H, W):
                break
            search *= 2
        sy, sx = np.unravel_index(np.argmin(cost), cost.shape)
        sy += wy0
        sx += wx0
        missing = known[y0:y0 + patch, x0:x0 + patch] == 0
        target[missing] = out[sy:sy + patch, sx:sx + patch][missing]
        known[y0:y0 + patch, x0:x0 + patch] = 1
    return np.clip(out, 0, 255).astype(np.uint8)
