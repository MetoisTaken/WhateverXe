#!/usr/bin/env python3
"""Bound bootstrap and kext-dispatch frames using Clang stack-usage output.

These are per-function limits, not a whole-kernel call-chain proof. Leave room
for Lilu/IOKit callers, the display helpers and interrupt handling; DEBUG must
not rely on optimization to coalesce large snapshot temporaries.
"""
import json
import sys
from pathlib import Path

LIMITS = {
    '_ZN2XE13processKernelER13KernelPatcherP10DeviceInfo': 2048,
    '_ZN2XE13adlnBootstrapEP11IOPCIDevice': 5120,
    '_ZN2XE11processKextER13KernelPatchermym': 256,
    '_ZN2XE21processTGLAcceleratorER13KernelPatchermym': 6144,
}

def check(path):
    frames = {}
    for line in Path(path).read_text().splitlines():
        location, size, kind = line.split('\t')
        symbol = location.rsplit(':', 1)[-1]
        if symbol in LIMITS:
            if symbol in frames:
                raise ValueError(f'duplicate stack record: {symbol}')
            frames[symbol] = {'bytes': int(size), 'kind': kind, 'limit': LIMITS[symbol]}
    for symbol, limit in LIMITS.items():
        if symbol not in frames:
            raise ValueError(f'missing stack record: {symbol}')
        frame = frames[symbol]
        if frame['kind'] != 'static' or frame['bytes'] > limit:
            raise ValueError(f'bootstrap stack exceeds budget: {symbol}: {frame}')
    return frames

if __name__ == '__main__':
    print(json.dumps(check(sys.argv[1]), indent=2))
