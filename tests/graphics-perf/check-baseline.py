"""Check copied loops against git and the first measured firmware's sources."""
import hashlib
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
REVISION = "519a857001220a54ed0c9224be1d741a84b362a1"
HASHES = {
    # Canonical LF hashes: git core.autocrlf must not change the verdict.
    # Raw first-run file hashes are also recorded in results/first-board-run.md.
    "old_vga.c": "4929588da56c444e0b3d2d202fd6f56058500255a42a12cd10795dbdbd7a4b0a",
    "old_text.c": "4bb74614b2659fbfae5947f6c80930a632707110b641485b403303b260787eef",
    "old_hdmi.c": "73cfb6065e31ce5c25a7f588b3b0689d4c5dd202569bd0e169d8f458043f799a",
    "old_ntsc.c": "a4a3f998b47684930ed4854601dbdcb80b51e1a4f7bccd34822cd5279acf7e75",
    "old_ntsc_text.c": "4e1d44e7513b27eedfe3121bd44073f9b2907248e39774dea9d03e3588531e6e",
}


def original(path):
    return subprocess.check_output(
        ["git", "-C", str(ROOT), "show", f"{REVISION}:{path}"]
    ).decode("utf-8").replace("\r\n", "\n")


def part(text, start, end, after=0):
    begin = text.index(start, after)
    return text[begin:text.index(end, begin)]


vga = original("drivers/vga-nextgen/vga.c")
hdmi = original("drivers/hdmi/hdmi.c")
ntsc = original("drivers/ntsc-tv/graphics.h")
text_loop = part(vga, "    switch (graphics_mode) {", "    if (screen_line & 1")
text_loop = re.sub(r"^\s*dma_channel_set_read_addr\([^\n]+\n", "", text_loop, flags=re.M)
text_loop = re.sub(r"^\s*port3DA \|= 1;[^\n]+\n", "", text_loop, flags=re.M)
ntsc_loop = part(ntsc, "            case COMPOSITE_160x200x16_force:",
                 "    // Generate vertical blanking lines after active video")
ntsc_loop = ntsc_loop[:ntsc_loop.rindex("    }")]
LOOPS = {
    "old_vga.c": part(vga, "    switch (graphics_mode) {",
                      "    dma_channel_set_read_addr(dma_channel_control, output_buffer, false);",
                      vga.index("    uint8_t *output_buffer_8bit;")),
    "old_text.c": text_loop,
    "old_hdmi.c": part(hdmi, "            case CGA_320x200x4:", "        // HSYNC"),
    "old_ntsc.c": ntsc_loop,
    "old_ntsc_text.c": part(ntsc, "            case TEXTMODE_40x25_BW:",
                            "            case COMPOSITE_160x200x16_force:"),
}

for name, expected_hash in HASHES.items():
    data = (HERE / name).read_bytes()
    if hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest() != expected_hash:
        raise SystemExit(f"FAIL: {name} changed since the first board run")
    if LOOPS[name].strip() not in data.decode("utf-8").replace("\r\n", "\n"):
        raise SystemExit(f"FAIL: {name} no longer contains the old pixel loop")
    print(f"PASS: {name}: first-run hash and original pixel loop")
