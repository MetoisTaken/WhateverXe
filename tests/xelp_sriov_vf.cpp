#include <cassert>
#include "kern_xelp_sriov_vf.hpp"

int main() {
    using namespace XELPSRIOV;
    static_assert(A780Identity == 0xA7808086U);
    static_assert(PFBar2Length == 0x40000000ULL);
    static_assert(VFCapabilityRegister == 0x1901F8U);
    static_assert(MMIOUpdateGGTTWords == 4U);

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
    puts("PASS SR-IOV VF contract classification and GuC relay ABI constants");
}
