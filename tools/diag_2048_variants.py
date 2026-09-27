#!/usr/bin/env python3
"""Generate 2048 variants to locate the xtensa wamrc O3 crash trigger."""
import pathlib
import re
import shutil
import subprocess
import sys
import os

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "guest/apps/2048"
base = pathlib.Path("/tmp/v2048")
shutil.copytree(src, base, dirs_exist_ok=True)


def write_variant(name, transform):
    out = pathlib.Path(f"/tmp/v{name}")
    shutil.copytree(base, out, dirs_exist_ok=True)
    p = out / "game2048_app.cpp"
    s = p.read_text()
    transform(s, p)


def variant_a(s, p):
    # Prevent aggressive O3 inlining of the small helpers.
    s = s.replace(
        "uint32_t SlideLine(uint32_t* line, uint32_t count)",
        "__attribute__((noinline)) uint32_t SlideLine(uint32_t* line, uint32_t count)",
        1,
    )
    s = s.replace(
        "void Transpose(uint32_t (*board)[kGridSize])",
        "__attribute__((noinline)) void Transpose(uint32_t (*board)[kGridSize])",
        1,
    )
    s = s.replace(
        "void ReverseRows(uint32_t (*board)[kGridSize])",
        "__attribute__((noinline)) void ReverseRows(uint32_t (*board)[kGridSize])",
        1,
    )
    p.write_text(s)


def variant_b(s, p):
    # TileColor switch -> lookup table (value is a power of two).
    table = '''micropixel::Color TileColor(uint32_t value) {
    static const micropixel::Color palette[12] = {
        micropixel::Color::Rgb(237U, 194U, 46U),
        micropixel::Color::Rgb(238U, 228U, 218U),
        micropixel::Color::Rgb(237U, 224U, 200U),
        micropixel::Color::Rgb(242U, 177U, 121U),
        micropixel::Color::Rgb(245U, 149U, 99U),
        micropixel::Color::Rgb(246U, 124U, 95U),
        micropixel::Color::Rgb(246U, 94U, 59U),
        micropixel::Color::Rgb(237U, 207U, 114U),
        micropixel::Color::Rgb(237U, 204U, 97U),
        micropixel::Color::Rgb(237U, 200U, 80U),
        micropixel::Color::Rgb(237U, 197U, 63U),
        micropixel::Color::Rgb(237U, 194U, 46U),
    };
    uint32_t index = 0U;
    while (value > 1U && index < 11U) { value >>= 1U; index += 1U; }
    return palette[index];
}'''
    start = s.index("micropixel::Color TileColor(uint32_t value) {")
    end = s.index("micropixel::Color TileTextColor(uint32_t value) {")
    s = s[:start] + table + "\n\n" + s[end:]
    p.write_text(s)


def variant_c(s, p):
    # Both: noinline + lookup table.
    variant_a(s, p)
    variant_b(s, p)


def variant_d(s, p):
    # no lambda capturing: replace SlideLine body with a plain loop (avoid
    # while-loop + array patterns under O3).  Use fixed unrolled version.
    old_start = s.index("uint32_t SlideLine(uint32_t* line, uint32_t count) {")
    old_end = s.index("// Transposes the board in place")
    body = '''uint32_t SlideLine(uint32_t* line, uint32_t count) {
    uint32_t compact[4]{};
    uint32_t compact_count = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        if (line[i] != 0U) {
            compact[compact_count++] = line[i];
        }
    }
    uint32_t out[4]{};
    uint32_t out_count = 0U;
    uint32_t score = 0U;
    uint32_t index = 0U;
    while (index < compact_count) {
        const uint32_t cur = compact[index];
        if (index + 1U < compact_count && cur == compact[index + 1U]) {
            out[out_count++] = cur * 2U;
            score += cur * 2U;
            index += 2U;
        } else {
            out[out_count++] = cur;
            index += 1U;
        }
    }
    bool changed = false;
    for (uint32_t i = 0U; i < count; ++i) {
        if (line[i] != out[i]) {
            changed = true;
        }
        line[i] = out[i];
    }
    return changed ? score : 0U;
}

'''
    s = s[:old_start] + body + s[old_end:]
    p.write_text(s)


variants = {
    "A": variant_a,
    "B": variant_b,
    "C": variant_c,
    "D": variant_d,
}

env = dict(os.environ)
results = {}
for name, fn in variants.items():
    write_variant(name, fn)
    out = pathlib.Path(f"/tmp/out-{name}")
    cmd = [
        sys.executable,
        str(root / "tools/micropixel"),
        "build",
        str(pathlib.Path(f"/tmp/v{name}")),
        "--aot-target",
        "xtensa",
        "--output-dir",
        str(out),
    ]
    print(f"########## VARIANT {name}")
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    tail = "\n".join(r.stdout.strip().splitlines()[-4:])
    print(tail)
    if r.stderr.strip():
        print("STDERR:", r.stderr.strip().splitlines()[-4:])
    results[name] = r.returncode
    print(f"variant-{name} exit={r.returncode}")

print("=========== VARIANTS DONE", results)
