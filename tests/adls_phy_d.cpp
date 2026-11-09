#include "kern_alderlake_s.hpp"
#include "kern_xelp_combo_clock.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace XELPComboClock;

static ClockSnapshot idle() {
    ClockSnapshot s;
    s.layout=Layout::ADLS;
    s.mux[0]=0x01000C00; // PHY A/B/C gated
    s.mux[1]=0x30;       // PHY D/E gated
    return s;
}

int main() {
    // Select and ungate are separate writes, and PHY E's select and gate survive.
    {
        uint32_t value=0x3B;
        std::vector<uint32_t> writes;
        auto read=[&](uint32_t r){assert(r==PhyDClock::reg);return value;};
        auto write=[&](uint32_t r,uint32_t v){assert(r==PhyDClock::reg);writes.push_back(v);value=v;};
        PhyDClock::enable(read,write,1);
        assert((writes==std::vector<uint32_t>{0x39,0x29}));
        PhyDClock::disable(read,write);
        assert(writes.size()==3 && writes[2]==0x39);
    }
    // PHY D allocates only from the three donor PLL slots, never PLL 3 by number.
    {
        ClockAllocation a;
        auto s=idle();
        auto c=a.choose(Port(3),s,true);
        assert(c.valid && c.pll==0 && !c.keepPLL);
        s.enable[0]=0xCC000000U;
        c=a.choose(Port(3),s,true);
        assert(c.valid && c.pll==1);
        s.enable[1]=s.enable[2]=0xCC000000U;
        assert(!a.choose(Port(3),s,true).valid);
        assert(!a.choose(Port(4),idle(),true).valid);
    }
    // A bound PHY D keeps its PLL out of the other PHYs' choices.
    {
        ClockAllocation a;
        a.binding[3]=1;
        auto c=a.choose(Port(0),idle(),true);
        assert(c.valid && c.pll==1);
    }
    // An active PHY D reports its CFGCR1 select, and an external owner blocks it.
    {
        ClockAllocation a;
        auto s=idle();
        s.mux[1]=0x22; // D ungated on PLL 2, E gated
        auto c=a.choose(Port(3),s,true);
        assert(c.valid && c.pll==2);
        a.external[3]=1;
        assert(!a.choose(Port(3),idle(),true).valid);
    }
    // Other layouts have no PHY D.
    {
        ClockAllocation a;
        auto s=idle();
        s.layout=Layout::TGL;
        assert(!a.choose(Port(3),s,true).valid);
    }
    // The donor's slot 2 PLL accesses move to the chosen PLL; the PHY C mux is untouched.
    {
        const ClockTranslation t{3,1,true};
        const uint32_t pc=0x6D000;
        assert(t.reg(0x46018,pc)==0x46014);
        assert(t.reg(0x164294,pc)==0x16428C);
        assert(t.reg(0x164298,pc)==0x164290);
        assert(t.reg(0x46018,0x1000)==0x46018);
        assert(t.mux(0x1234,0x6D77E)==0x1234);
    }
    // AGDC refusals: no answer, or an answer naming a configuration that does not exist.
    {
        using T=AlderLakeS::AGDCTiming;
        assert(T::refused(0x100,0,1));
        assert(T::refused(1,1,1));
        assert(T::refused(1,0xFFFFFFFFU,1));
        assert(!T::refused(1,0,1));
        assert(T::nativeAvailable(1,5,0));
        assert(!T::nativeAvailable(0,5,0));
        assert(!T::nativeAvailable(1,2,0));
        assert(!T::nativeAvailable(1,5,1));
    }
    puts("PASS ADL-S PHY D clock and fixed DP AGDC timing decisions");
}
