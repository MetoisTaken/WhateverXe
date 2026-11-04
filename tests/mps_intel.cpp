#include "kern_mps_intel.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace MPSIntel;

int main() {
    // Excerpt of macOS 26.7.1 dyld_shared_cache_x86_64h.map.
    const char map[] =
        "mapping  EX  619MB 0x7FF800000000 -> 0x7FF826BE4000\n"
        "/System/Library/Frameworks/MetalPerformanceShaders.framework/Versions/A/Frameworks/MPSCore.framework/Versions/A/MPSCoreX\n"
        "\t          __TEXT 0x7FF8000BC000 -> 0x7FF8000CDBDE\n"
        "/System/Library/Frameworks/MetalPerformanceShaders.framework/Versions/A/Frameworks/MPSCore.framework/Versions/A/MPSCore\n"
        "\t          __TEXT 0x7FF8107BC000 -> 0x7FF81087DBDE\n"
        "\t    __DATA_CONST 0x7FF841406070 -> 0x7FF84141C130\n";
    uint64_t start=0,end=0;
    assert(textRange(map,sizeof(map)-1,start,end));
    assert(start==0x7FF8107BC000ULL && end==0x7FF81087DBDEULL);

    // Not present, truncated or missing __TEXT: no range.
    const char none[]="/System/Library/Frameworks/Metal.framework/Versions/A/Metal\n\t__TEXT 0x1 -> 0x2\n";
    assert(!textRange(none,sizeof(none)-1,start,end));
    const char noText[]="/System/Library/Frameworks/MetalPerformanceShaders.framework/Versions/A/Frameworks/MPSCore.framework/Versions/A/MPSCore\n\t__DATA 0x10 -> 0x20\n";
    assert(!textRange(noText,sizeof(noText)-1,start,end));

    // The patch turns the SGX jne into nop; jmp with the same displacement.
    assert(Find[JumpOffset]==Original[0] && Find[JumpOffset+1]==Original[1]);
    assert(Patched[0]==0x90 && Patched[1]==0xE9);
    assert(JumpOffset+6==sizeof(Find));
    puts("PASS MPSCore map parsing and SGX branch patch layout");
}
