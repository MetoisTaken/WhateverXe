#!/usr/bin/env python3
"""Patch the Xe compiler adapter (XeCompilerAdapter.bundle/.../adapter.dylib).

Two defects in the shipped adapter break Metal on macOS 26:

1. XeBuildUntranslated rejects any compute state whose
   maxTotalThreadsPerThreadgroup (state offset 0x9C) is set, so pipelines
   built from such descriptors fail with "unsupported compute state".
   The field is cleared in the 10-byte alignment nop before the zero check;
   the IGC build already supports the full threadgroup size.
2. After one IGC failure the error text stays in the context (offset 0x60)
   and every later build returns it without compiling. The entry check is
   replaced by clearing that buffer.

Usage: patch_xe_adapter.py <adapter.dylib> [<output>]   (re-sign ad hoc after)
"""
import hashlib, sys

ORIGINAL_SHA256 = "11609123ce5b930a"  # prefix of the shipped adapter
PATCHES = [
    (0xE16, "662e0f1f840000000000", "c7869c00000000000000"),  # movl $0, 0x9c(%rsi)
    (0xD8A, "807f60007414", "c6476000eb14"),                  # movb $0, 0x60(%rdi); jmp
]
CONTEXT = (0xE04, "4183fe03754db807000000807c06fd007535")


def main() -> int:
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src
    data = bytearray(open(src, "rb").read())
    digest = hashlib.sha256(data).hexdigest()
    off, ctx = CONTEXT
    if data[off:off + len(ctx) // 2] != bytes.fromhex(ctx):
        print("unexpected adapter layout", digest)
        return 1
    for off, old, new in PATCHES:
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        cur = bytes(data[off:off + len(old)])
        if cur == new:
            continue
        if cur != old:
            print(f"unexpected bytes at {off:#x}: {cur.hex()}")
            return 1
        data[off:off + len(new)] = new
    open(dst, "wb").write(data)
    print("patched", digest[:16], "->", hashlib.sha256(data).hexdigest()[:16])
    return 0


if __name__ == "__main__":
    sys.exit(main())
