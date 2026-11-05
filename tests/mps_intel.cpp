#include "kern_mps_intel.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace MPSIntel;
template<class T,size_t N> constexpr size_t arrsize_test(const T (&)[N]) {return N;}

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
    // MPSGraph text range by path, and the stitching patch layout: the and
    // that clears the option bit is kept verbatim and the jmp lands right
    // after the log call, where the original je (no variable) also lands.
    const char graphMap[] =
        "/System/Library/Frameworks/MetalPerformanceShadersGraph.framework/Versions/A/MetalPerformanceShadersGraph\n"
        "\t          __TEXT 0x7FF818367000 -> 0x7FF8196EB000\n";
    assert(textRange(graphMap,sizeof(graphMap)-1,start,end,GraphPath));
    assert(start==0x7FF818367000ULL && end==0x7FF8196EB000ULL);
    assert(!textRange(graphMap,sizeof(graphMap)-1,start,end));
    // je +0 lands on the option write; the first jmp lands on the disable
    // byte write; the second jmp lands at the end, where the original je
    // (variable absent) landed. Patched bytes are in strictly safe order.
    assert(sizeof(GraphFind)==49 && GraphFind[3]==0x74 && 5+GraphFind[4]==sizeof(GraphFind));
    uint8_t g[sizeof(GraphFind)];graphPatched(g);
    assert(g[3]==0x74 && g[4]==0 && g[5]==0x66);
    assert(g[14]==0xEB && 16+g[15]==26 && g[26]==0xC6 && g[32]==0x01);
    assert(g[33]==0xEB && 35+g[34]==sizeof(GraphFind));
    assert(GraphPatch[arrsize_test(GraphPatch)-1].offset==4);
    puts("PASS MPSCore map parsing and SGX branch patch layout");
    puts("PASS MPSGraph map parsing and stitching patch layout");
}
