"""Turns QB_XR_CAPTURE's raw eye images into PNGs.

    python tools/xr_frames.py CAPTURE_DIR

Each frameNNN_scK_WxH_fF.rgba becomes a .png beside it, and each frame with
two eyes also gets frameNNN_both.png, left and right side by side.
The bytes are the swapchain's own; an sRGB format (43) is already display
ready, so nothing is converted.
"""

import glob
import os
import re
import sys

from PIL import Image


def main():
    directory = sys.argv[1]
    frames = {}
    for path in sorted(glob.glob(os.path.join(directory, "*.rgba"))):
        m = re.search(r"frame(\d+)_sc(\d+)_(\d+)x(\d+)_f(\d+)\.rgba$", path)
        if not m:
            continue
        frame, chain, width, height = (int(m.group(i)) for i in range(1, 5))
        data = open(path, "rb").read()
        if len(data) != width * height * 4:
            print(f"{path}: {len(data)} bytes, expected {width * height * 4}")
            continue
        image = Image.frombytes("RGBA", (width, height), data).convert("RGB")
        image.save(path[:-5] + ".png")
        frames.setdefault(frame, []).append((chain, image))
    for frame, eyes in sorted(frames.items()):
        if len(eyes) < 2:
            continue
        eyes.sort()
        width = sum(image.width for _, image in eyes)
        height = max(image.height for _, image in eyes)
        both = Image.new("RGB", (width, height))
        x = 0
        for _, image in eyes:
            both.paste(image, (x, 0))
            x += image.width
        out = os.path.join(directory, f"frame{frame:03d}_both.png")
        both.thumbnail((1800, 1800))
        both.save(out)
        print(out)


if __name__ == "__main__":
    main()
