#include <cassert>
#include <cstdio>
#include "kern_xelp_sriov_vf.hpp"

int main() {
    using namespace XELPSRIOV;
    static_assert(A780Identity == 0xA7808086U);
    static_assert(PFBar2Length == 0x40000000ULL);
    static_assert(VFCapabilityRegister == 0x1901F8U);
    static_assert(MMIOUpdateGGTTWords == 4U);
    static_assert(MMIORelayAction == 0x5005U);
    static_assert(MMIOScratchBase == 0x190240U);
    static_assert(MMIOHostInterrupt == 0x1901F0U);
    static_assert(mmioRelayHeader(MMIOHandshakeOpcode) == 0x0F015005U);
    static_assert(packVersion(1U, 2U) == 0x00010002U);
    static_assert(packGGTTControl(0x12345000U, GGTTMode::Replicate, 7U) ==
                  0x12345407U);

    const MMIORequest handshake = makeHandshakeRequest(1U, 2U);
    assert(handshake.count == MMIOHandshakeWords);
    assert(handshake.words[0] == 0x0F015005U);
    assert(handshake.words[1] == 0x00010002U);
    assert(requestHeaderValid(handshake.words[0], MMIOHandshakeOpcode));

    const MMIORequest ggtt = makeGGTTRequest(0x12345000U,
                                               GGTTMode::Replicate,
                                               7U,
                                               0x1122334455667788ULL);
    assert(ggtt.words[0] == 0x0F025005U);
    assert(ggtt.words[1] == 0x12345407U);
    assert(ggtt.words[2] == 0x55667788U);
    assert(ggtt.words[3] == 0x11223344U);
    assert(requestHeaderValid(ggtt.words[0], MMIOUpdateGGTTOpcode));

    const MMIORequest runtime = makeRuntimeRequest(0x9118U, 0x9134U);
    assert(runtime.words[0] == 0x0F105005U);
    assert(runtime.words[1] == 0x9118U);
    assert(runtime.words[2] == 0x9134U);
    assert(runtime.words[3] == 0U);
    assert(requestHeaderValid(runtime.words[0], MMIOGetRuntimeOpcode));
    assert(responseHeaderValid(0xFF000000U));
    assert(!responseHeaderValid(0x6F000000U));
    assert(responseMajor(0x00010002U) == 1U);
    assert(responseMinor(0x00010002U) == 2U);

    Contract vf {A780Identity,0x0002,VFBar0Length,0x10000000ULL,true,true};
    assert(vf.identityMatches());
    assert(vf.aperturesMatch());
    assert(vf.classified());
    assert(vf.memoryEnabled());
    assert(vf.gucRelayRequired());

    Contract pf {A780Identity,0x0002,VFBar0Length,PFBar2Length,true,false};
    assert(!pf.aperturesMatch());
    assert(!pf.classified());
    assert(!pf.gucRelayRequired());

    Contract disabled = vf;
    disabled.command = 0;
    assert(disabled.classified());
    assert(!disabled.gucRelayRequired());

    Contract wrong = vf;
    wrong.identity = 0x12348086U;
    assert(!wrong.classified());
    assert((0x12345000U & MMIOUpdateGGTTOffsetMask) == 0x12345000U);
    assert((0x00000C00U & MMIOUpdateGGTTModeMask) == MMIOUpdateGGTTModeMask);
    puts("PASS SR-IOV VF contract classification and GuC MMIO relay packing");
}
