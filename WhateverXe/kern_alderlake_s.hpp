// ADL-S coordinates from Linux v6.12 intel_bios.c, intel_ddi.c and i915_reg.h.
// Physical DDI, Combo PHY and GMBUS pin are distinct namespaces. This module
// describes hardware; it does not grant donor or runtime admission.
#ifndef KERN_ALDERLAKE_S_HPP
#define KERN_ALDERLAKE_S_HPP
#include <stdint.h>
#include "kern_vbt.hpp"
#include "kern_xelp_panel_power.hpp"
#include "kern_xelp_display_resources.hpp"
#include "kern_xelp_gt_resources.hpp"

namespace AlderLakeS {
constexpr uint32_t PortMask=XELP::ADLSComboResources::physicalMask(); // A, TC1, TC2, TC3, TC4
// ADL-S/RPL-S share the GT backend across revisions; physical identity is never spoofed.
inline bool identity(uint32_t pci,uint32_t revision) {return revision<=255 && XELP::desktopGTDevice(pci);}
struct GT : XELP::SmallGTResources {
    static bool decode(uint32_t pci,uint32_t revision,const uint32_t *fuses,GT &out) {
        out={};
        return identity(pci,revision) && XELP::decodeSmallGT(fuses,out);
    }
};
static_assert(sizeof(GT)==6*sizeof(uint32_t),"Preserve GT resource layout");
// The donor has three Combo clock slots. D/E need a separate clock allocator.
constexpr uint32_t ComboBackendMask=XELP::ADLSComboBackend::mask(XELP::Donor::C004FC1A); // physical A/TC1/TC2, PHY A/B/C
struct Port {
    uint32_t ddi {},phy {},buffer {},clock {},clockShift {},clockOff {},hpd {};
    static bool resolve(uint32_t ddi,Port &out) {
        out={};
        if(ddi>6 || !(PortMask&(1U<<ddi)))return false;
        const auto phy=XELP::ADLSComboResources::phy(ddi);
        const uint32_t off[]={10,11,24,4,5};
        out={ddi,phy,0x64000+0x100*ddi,phy<3?0x164280U:0x1642BCU,
             2*(phy%3),1U<<off[phy],1U<<(ddi?ddi+21:16)};
        return true;
    }
    uint32_t phyBase() const {
        const uint32_t bases[]={0x162000,0x6C000,0x160000,0x161000,0x16B000};
        return bases[phy];
    }
    uint32_t aux() const {
        const uint32_t channels[]={0x40,0x10,0x20,0x30,0x50};
        return channels[phy];
    }
    bool connected(uint32_t sde) const {return sde!=UINT32_MAX && (sde&hpd);}
};
// ADL-S uses TGL PW_2/PW_3 for physical TC lane domains, including Combo
// PHY HDMI. Native pipe refs do not include this independent lane consumer.
struct LanePower {
    static uint32_t target(uint32_t native,bool tc1) {
        return native | (tc1 ? 0x2AU : 0U);
    }
    static uint32_t pipeRequests(const uint32_t *refs) {
        uint32_t out=0;
        for(unsigned i=0;i<4;++i)if(refs[i])out|=2U<<(2*i);
        return out;
    }
};
// VBT numbers A/TC1..4 as 1..5; GMBUS uses 1/9..12. Zero means absent.
inline uint32_t gmbus(uint32_t vbtPin) {
    return vbtPin==1?1:vbtPin>=2 && vbtPin<=5?vbtPin+7:0;
}
// Immutable firmware-derived routing shared by startup, clocks, training and
// hotplug. Unsupported or ambiguous routes stay excluded; never guess a port.
struct Wiring {
    uint32_t hdmi {},dp {},excluded {},pin[XELP::PortCount] {},panel {};
    bool valid {},excludeDonorPanel {};
    uint32_t selected() const {return hdmi|dp|panel;}
    bool hasDP(uint32_t d) const {return valid && d<XELP::PortCount && (dp&(1U<<d));}
    bool matches(const uint32_t *c,bool wantDP) const {
        if(!valid || !c || c[0]>=XELP::PortCount ||
           !((wantDP?dp:hdmi)&(1U<<c[0])))return false;
        return c[1]==pin[c[0]] && c[2]==(wantDP?1U:2U) && !c[3] &&
            c[4]==(wantDP?0x400U:0x800U) && c[5]==(wantDP?0x201U:0x202U);
    }
    bool matchesPanel(const uint32_t *c) const {
        const uint32_t expected[]={0,0,0,0,2,0x18};
        return valid && panel==1 && !(panel&(hdmi|dp)) && c && !memcmp(c,expected,sizeof(expected));
    }
    bool matches(const uint32_t *c) const {return matches(c,false) || matches(c,true) || matchesPanel(c);}
    bool bound(const void *platform,const uint32_t *c) const {
        return platform && matches(c) && c==reinterpret_cast<const uint32_t *>(
            static_cast<const uint8_t *>(platform)+0x20+24*c[0]);
    }
    bool boundDP(const void *platform,const uint32_t *c) const {return matches(c,true) && bound(platform,c);}
    bool boundPanel(const void *platform,const uint32_t *c) const {return matchesPanel(c) && bound(platform,c);}
    bool boundSST(const void *platform,const uint32_t *c) const {return boundDP(platform,c) || boundPanel(platform,c);}
    bool usesDDC(uint32_t bus) const {
        if(!valid)return false;
        for(unsigned d=0;d<XELP::PortCount;++d)if((hdmi&(1U<<d)) && pin[d]==bus)return true;
        return false;
    }
    uint32_t laneMask() const {
        uint32_t mask=0;
        for(unsigned d=3;d<XELP::PortCount;++d)if(selected()&(1U<<d))mask|=2U<<(2*d);
        return valid?mask:0;
    }
    bool laneConsumer(const uint32_t *refs) const {
        if(!valid || !refs)return false;
        for(unsigned d=3;d<XELP::PortCount;++d)if((selected()&(1U<<d)) && refs[d])return true;
        return false;
    }
    uint32_t hotplugMode(bool ready,const uint32_t *c,const void *platform,
                         uintptr_t caller,uintptr_t hpdCaller,uint32_t requested) const {
        return ready && boundDP(platform,c) && hpdCaller && caller==hpdCaller && requested==4 ? 1 : requested;
    }
    bool apply(void *platform,size_t bytes) const {
        if(!valid || !selected() || (panel&~1U) || (panel&(hdmi|dp)) || (selected()&~ComboBackendMask) || !platform || bytes<0x1C0)return false;
        auto p=static_cast<uint8_t *>(platform);
        if(p[0x12]!=9)return false;
        if(excludeDonorPanel) {
            // Native word4=1 is the dummy connector; zero word5 removes
            // the donor's unconditional panel-connected flag. Never apply
            // this to a selected A route or an unrecognized predecessor.
            const uint32_t panel[]={0,0,0,0,2,0x18};
            if((selected()&1) || memcmp(p+0x20,panel,sizeof(panel)))return false;
            for(unsigned d=0;d<XELP::PortCount;++d)
                if(*reinterpret_cast<const uint32_t *>(p+0x20+24*d)!=d)return false;
        }
        // The exact donor's platform tail selects no TCON / Camellia / Banksia.
        // VBT-qualified PWM uses native PPS, not an Apple TCON protocol.
        const auto tcon=*reinterpret_cast<const uint32_t *>(p+0x190);
        if(panel && tcon!=0 && tcon!=2 && tcon!=3)return false;
        // Check every selected predecessor before mutating any record.
        for(unsigned d=0;d<XELP::PortCount;++d)if(selected()&(1U<<d)) {
            const uint32_t expected[]={d,d>=3?d+6:d+1,1,d>=3?1U:0U,0x400,0x201};
            const uint32_t panel[]={0,0,0,0,2,0x18};
            if(memcmp(p+0x20+24*d,expected,sizeof(expected)) &&
               (d || memcmp(p+0x20,panel,sizeof(panel))))return false;
        }
        for(unsigned d=0;d<XELP::PortCount;++d)if(selected()&(1U<<d)) {
            auto c=reinterpret_cast<uint32_t *>(p+0x20+24*d);
            if(panel&(1U<<d)){const uint32_t nativePanel[]={0,0,0,0,2,0x18};memcpy(c,nativePanel,sizeof(nativePanel));continue;}
            const bool isDP=dp&(1U<<d);
            c[1]=pin[d];c[2]=isDP?1:2;c[3]=0;c[4]=isDP?0x400:0x800;c[5]=isDP?0x201:0x202;
        }
        if(panel)*reinterpret_cast<uint32_t *>(p+0x190)=0;
        if(excludeDonorPanel) {
            const uint32_t excluded[]={1,0};
            memcpy(p+0x20+16,excluded,sizeof(excluded));
        }
        return true;
    }
    static Wiring fromFirmware(const XELP::VBT::Ports &ports) {
        Wiring out;
        if(!ports.valid())return out;
        out.excluded=ports.present;
        if(XELP::PanelPower::nativePPS0(ports,XELP::Family::AlderLakeS)) {
            const uint32_t zero[5]={};
            if(XELP::PanelPower::select(ports.panel,zero,0,0,0).valid){out.panel=1;out.excluded&=~1U;}
        }
        for(unsigned d=0;d<XELP::PortCount;++d) {
            if(!(ports.present&ComboBackendMask&(1U<<d)))continue;
            const auto &p=ports.port[d];Port port;
            if(!Port::resolve(d,port) || XELP::VBT::ddi(XELP::Family::AlderLakeS,p.dvo)!=d)continue;
            const bool isHDMI=p.fixedTMDS();
            const bool isDP=(p.type&0x14)==0x14 && !(p.type&(0x1000|0x400|0x20|1)) &&
                !(p.flags&0x17) && !(p.capabilities&3);
            if(!isHDMI && !isDP)continue;
            // The native AUX engine is tied to physical DDI. Cross-routed
            // AUX requires its own backend; DDC is independently programmable.
            const auto bus=isHDMI?gmbus(p.ddc):(d>=3?d+6:d+1);
            if(!bus || (isDP && p.aux!=port.aux()))continue;
            bool shared=false;
            for(unsigned other=0;other<XELP::PortCount;++other)if(other!=d && (ports.present&(1U<<other))) {
                const auto &q=ports.port[other];
                shared|=isDP ? (q.dp() && q.aux==p.aux) : (q.ddc && q.ddc==p.ddc);
            }
            if(shared)continue;
            (isDP?out.dp:out.hdmi)|=1U<<d;out.pin[d]=bus;out.excluded&=~(1U<<d);
        }
        out.excludeDonorPanel=ports.externalOnly && !ports.unmapped &&
            !ports.panel.valid && !(ports.present&1) && !out.panel;
        out.valid=true;return out;
    }
};
struct PLL {
    uint32_t enable {},cfg0 {},cfg1 {};
    static bool resolve(uint32_t id,PLL &out) {
        out={};if(id>3)return false;
        const uint32_t enables[]={0x46010,0x46014,0x46018,0x46030};
        const uint32_t configs[]={0x164284,0x16428C,0x164294,0x1642C0};
        out={enables[id],configs[id],configs[id]+4};return true;
    }
};
// Keep physical DDI/AUX registers; populate only the separate Combo bank.
// PHY A has a native table: validate it and correct its duplicated lane2 slot.
inline bool comboRegisters(uint32_t ddi,uint32_t *registers,size_t count) {
    Port port;
    if(!Port::resolve(ddi,port) || !(ComboBackendMask&(1U<<ddi)) || !registers ||
       count!=0x128/4 || registers[4]!=port.buffer ||
       registers[(0x64C-0x580)/4]!=(ddi?0x46100+4*ddi:0))return false;
    const uint32_t offsets[]={
        0,0x100,0x104,0x10C,0x124,0x128,0x014,0x028,0x030,0x03C,
        0x688,0x888,0x988,0xA88,0xB88,0x388,
        0x690,0x890,0x990,0xA90,0xB90,0x390,
        0x694,0x894,0x994,0xA94,0xB94,0x394,
        0x604,0x804,0x904,0xA04,0xB04,0x304,
        0x624,0x824,0x924,0xA24,0xB24,0x324
    };
    static_assert(sizeof(offsets)==0x648-0x5AC+4,"Combo PHY register table span");
    auto bank=registers+(0x5AC-0x580)/4;
    for(unsigned i=0;i<sizeof(offsets)/4;++i) {
        const auto expected=i?port.phyBase()+offsets[i]:0x64C00+4*port.phy;
        // The exact donor duplicates lane3 TX_DW9 in its PHY A lane2 slot.
        const auto native=(!ddi && offsets[i]==0xA24)?expected+0x100:expected;
        if(bank[i]!=(ddi?0:native))return false;
    }
    for(unsigned i=0;i<sizeof(offsets)/4;++i)
        bank[i]=i?port.phyBase()+offsets[i]:0x64C00+4*port.phy;
    return true;
}
// Five-word ClockParams ABI at the exact donor's combo-HDMI call boundary.
// Keep logical divisors separate from register encodings. Display WA
// 22010492432 halves only the DCO fraction for a 38.4 MHz reference.
struct HDMIClock {
    uint32_t cfg0 {},cfg1 {};
    static bool encode(const uint32_t *params,uint32_t refCode,HDMIClock &out) {
        out={};if(!params || refCode>2 || !params[0] || params[0]>0x3FF ||
            params[1]>0x7FFF || !params[3] || params[3]>255 || !params[4] || params[4]>3)return false;
        uint32_t p=0;
        switch(params[2]) {case 2:p=1;break;case 3:p=2;break;case 5:p=4;break;case 7:p=8;break;default:return false;}
        const auto fraction=refCode==2?(params[1]+1)/2:params[1];
        const uint32_t k[]={0,1,2,4};
        out.cfg0=params[0]|(fraction<<10);
        out.cfg1=(p<<2)|(k[params[4]]<<6);
        if(params[3]!=1)out.cfg1|=0x200|(params[3]<<10);
        return true;
    }
};
}
#endif
