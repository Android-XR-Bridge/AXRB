"""Checks tools/texture_transcode.h against texture2ddecoder (pip install texture2ddecoder numpy).

Random blocks exercise every ETC2 mode. Our ETC2/EAC decode must match the reference decoder, and the BC
blocks we emit are decoded by the reference BC decoder and compared with the source pixels (PSNR; random
blocks are noise, so only image content has a quality bar).
Usage: python tests/texture_transcode_check.py <texture_transcode_check.exe> [image to use instead of a synthetic one]
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
import texture2ddecoder as t2d

W = H = 512
CASES = [  # codec, block bytes, reference source decoder, reference BC decoder, channels compared, min PSNR
    ('etc2', 8, t2d.decode_etc2, t2d.decode_bc1, 'rgb', 30),
    ('etc2a1', 8, t2d.decode_etc2a1, t2d.decode_bc1, 'rgba', 28),
    ('etc2a8', 16, t2d.decode_etc2a8, t2d.decode_bc3, 'rgba', 30),
    ('eacr', 8, t2d.decode_eacr, t2d.decode_bc4, 'r', 34),
    ('eacrg', 16, t2d.decode_eacrg, t2d.decode_bc5, 'rg', 34),
]


def rgba(bgra):
    return np.frombuffer(bgra, np.uint8).reshape(H, W, 4)[..., [2, 1, 0, 3]].astype(np.int32)


def bc1_opaque(bc):
    """Per-texel opacity of BC1 blocks (texture2ddecoder's BC1 ignores punch-through alpha)."""
    blocks = np.frombuffer(bc, np.uint8).reshape(-1, 8)
    c0 = blocks[:, 0].astype(np.int32) | blocks[:, 1].astype(np.int32) << 8
    c1 = blocks[:, 2].astype(np.int32) | blocks[:, 3].astype(np.int32) << 8
    bits = blocks[:, 4:8].copy().view('<u4')[:, 0]
    index = (bits[:, None] >> (2 * np.arange(16))) & 3
    transparent = (c0 <= c1)[:, None] & (index == 3)
    tiles = (~transparent).reshape(H // 4, W // 4, 4, 4)
    return tiles.transpose(0, 2, 1, 3).reshape(H, W)


def synthetic_image(rng):
    """Texture-like content: smooth gradients, hard edges and mild noise."""
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    r = 128 + 100 * np.sin(x / 37) * np.cos(y / 53)
    g = 60 + 150 * (x / W) + 20 * ((x // 32 + y // 32) % 2)
    b = 200 - 150 * (y / H) + 40 * (np.hypot(x - 256, y - 256) < 120)
    image = np.stack([r, g, b], -1) + rng.normal(0, 4, (H, W, 3))
    return np.clip(image, 0, 255).astype(np.uint8)


ETC1_MODIFIERS = np.array([[2, 8], [5, 17], [9, 29], [13, 42], [18, 60], [24, 80], [33, 106], [47, 183]])


def etc1_encode(image):
    """Minimal ETC1 individual-mode encoder (flip 0), so real image content can be fed through the transcoder."""
    h, w, _ = image.shape
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            block = image[by:by + 4, bx:bx + 4, :3].astype(np.int32)
            word, lsb, msb = 0, 0, 0
            bases, tables = [], []
            for sub in range(2):
                pixels = block[:, sub * 2:sub * 2 + 2]
                base4 = np.clip(np.round(pixels.reshape(-1, 3).mean(0) / 17), 0, 15).astype(int)
                base = base4 * 17
                best = None
                for t, (a, b) in enumerate(ETC1_MODIFIERS):
                    mods = np.array([a, b, -a, -b])
                    cand = np.clip(base[None, None, None, :] + mods[None, None, :, None], 0, 255)
                    err = ((cand - pixels[:, :, None, :]) ** 2).sum(-1)
                    total = err.min(-1).sum()
                    if best is None or total < best[0]:
                        best = (total, t, err.argmin(-1))
                bases.append([int(v) for v in base4]); tables.append(int(best[1]))
                for y in range(4):
                    for x in range(2):
                        i = (sub * 2 + x) * 4 + y
                        index = int(best[2][y, x])
                        msb |= (index >> 1) << i; lsb |= (index & 1) << i
            word = (bases[0][0] << 60 | bases[1][0] << 56 | bases[0][1] << 52 | bases[1][1] << 48 |
                    bases[0][2] << 44 | bases[1][2] << 40 | tables[0] << 37 | tables[1] << 34 | msb << 16 | lsb)
            out += int(word).to_bytes(8, 'big')
    return np.frombuffer(bytes(out), np.uint8).reshape(-1, 8)


def main():
    tool = sys.argv[1]
    rng = np.random.default_rng(7)
    if len(sys.argv) > 2:  # optional real image (e.g. a game screenshot) instead of the synthetic one
        from PIL import Image
        image = np.asarray(Image.open(sys.argv[2]).convert('RGB').resize((W, H)))
    else:
        image = synthetic_image(rng)
    etc1 = etc1_encode(image)
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        for name, size, ref_source, ref_bc, channels, min_psnr in CASES:
            for kind in ('random', 'image') if name.startswith('etc2') else ('random',):
                count = (W // 4) * (H // 4)
                blocks = rng.integers(0, 256, (count, size), dtype=np.uint8)
                if kind == 'image':  # ETC1-encoded image content (valid ETC2); for RGBA, alpha from luminance
                    color = etc1
                    if size == 16:  # EAC alpha: multiplier 1, table 13 (fine steps)
                        alpha = np.zeros((count, 8), np.uint8)
                        alpha[:, 0] = image.mean(-1).astype(np.uint8)[::4, ::4].reshape(-1)
                        alpha[:, 1] = 0x1D
                        color = np.concatenate([alpha, color], 1)
                    blocks = np.ascontiguousarray(color)
                paths = [os.path.join(tmp, f) for f in ('in.bin', 'decoded.rgba', 'out.bin')]
                blocks.tofile(paths[0])
                result = subprocess.run([tool, name, str(W), str(H)] + paths, capture_output=True, text=True)
                if result.returncode:
                    print(result.stderr); return 1
                ours = np.fromfile(paths[1], np.uint8).reshape(H, W, 4).astype(np.int32)
                bc = open(paths[2], 'rb').read()
                ref = rgba(ref_source(blocks.tobytes(), W, H))
                idx = ['rgba'.index(c) for c in channels]
                if name == 'etc2a1':  # spec: transparent texels are (0,0,0,0); the reference keeps their RGB
                    visible = ref[..., 3] == 255
                    decode_diff = max(np.abs(ours[..., 3] - ref[..., 3]).max(), np.abs(ours[..., :3] - ref[..., :3])[visible].max())
                else:
                    decode_diff = np.abs(ours[..., idx] - ref[..., idx]).max()
                back = rgba(ref_bc(bc, W, H))
                src = ours
                if name == 'etc2a1':  # BC1 transparent texels decode as black; compare colour only where opaque
                    opaque = src[..., 3] >= 128
                    mse = np.mean((back[..., :3][opaque] - src[..., :3][opaque]) ** 2)
                    alpha_ok = np.array_equal(bc1_opaque(bc), opaque)
                else:
                    mse = np.mean((back[..., idx] - src[..., idx]) ** 2)
                    alpha_ok = True
                psnr = 10 * np.log10(255 ** 2 / max(mse, 1e-9))
                ok = decode_diff <= 1 and alpha_ok and (kind == 'random' or psnr >= min_psnr)
                failed |= not ok
                print(f"{name:7s} {kind:6s} decode max diff {decode_diff}, BC PSNR {psnr:5.1f} dB"
                      f"{'' if alpha_ok else ', ALPHA MISMATCH'}  {result.stdout.strip().split(': ')[1]}  {'ok' if ok else 'FAIL'}")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
