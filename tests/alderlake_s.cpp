#include "kern_alderlake_s.hpp"
#include <cassert>
#include <cstdio>
#include <climits>
#include <initializer_list>
using namespace AlderLakeS;
int main() {
    uint32_t fuses[]={1,3,0,0xF0,0xE00FA};GT gt;
    assert(GT::decode(0x46908086,0xC,fuses,gt) && gt.eus==32 && gt.nativeSubslices==4 && gt.l3Banks==4 && gt.urbKiB==256);
    unsigned cases=0;
    const unsigned ids[]={0x4680,0x4682,0x4688,0x468A,0x468B,0x4690,0x4692,0x4693,
                          0xA780,0xA781,0xA782,0xA783,0xA788,0xA789,0xA78A,0xA78B};
    for(auto id:ids)for(unsigned rev=0;rev<256;++rev) {
        assert(GT::decode((id<<16)|0x8086,rev,fuses,gt));
        assert(!GT::decode((id<<16)|0x1234,rev,fuses,gt));++cases;
    }
    for(unsigned dss=1;dss<64;++dss)for(unsigned eu=0;eu<256;++eu)for(unsigned video:{1U,5U}) {
        fuses[1]=dss;fuses[2]=eu;fuses[4]=0xE0000|(0xFF^video);
        const unsigned count=__builtin_popcount(dss),eus=2*count*(8-__builtin_popcount(eu));
        const bool supported=count<=2 && (eus==16 || eus==24 || eus==32);
        assert(GT::decode(0xA7808086,4,fuses,gt)==supported);
        if(supported)assert(gt.dss==count && gt.nativeSubslices==count*2 && gt.eus==eus && gt.l3Banks==4 && gt.urbKiB==256);
        else assert(!gt.eus);
        ++cases;
    }
    const uint32_t original[]={1,3,0,0xF0,0xE00FA};memcpy(fuses,original,sizeof(fuses));
    for(unsigned i=0;i<5;++i) {
        fuses[i]=UINT32_MAX;assert(!GT::decode(0xA7808086,4,fuses,gt));fuses[i]=original[i];
        fuses[i]|=0x80000000;assert(GT::decode(0xA7808086,4,fuses,gt));fuses[i]=original[i];
    }
    for(unsigned i:{0U,3U,4U}) {
        fuses[i]^=1;assert(!GT::decode(0xA7808086,4,fuses,gt));fuses[i]=original[i];
    }
    assert(!GT::decode(0x46D18086,0,fuses,gt));assert(!GT::decode(0x46908086,256,fuses,gt));
    assert(!GT::decode(0x46908086,0xC,nullptr,gt));
    printf("PASS %u desktop GT identity/step/resource cases\n",cases);
    const uint32_t physical[]={0,3,4,5,6},dvo[]={10,1,8,9,11};
    const uint32_t expectedPin[]={1,9,10,11,12};
    const uint32_t expectedHPD[]={0x10000,0x1000000,0x2000000,0x4000000,0x8000000};
    for(unsigned n=0;n<5;++n) {
        assert(XELP::VBT::ddi(XELP::Family::AlderLakeS,dvo[n])==physical[n]);
        Port port;assert(Port::resolve(physical[n],port));
        assert(port.phy==n && port.buffer==0x64000+physical[n]*256);
        assert(port.hpd==expectedHPD[n] && port.connected(expectedHPD[n]));
        assert(!port.connected(UINT32_MAX) && !port.connected(~expectedHPD[n]));
        assert(gmbus(n+1)==expectedPin[n]);
        assert(port.clock==(n<3?0x164280U:0x1642BCU));
    }
    for(auto d:{1U,2U,7U,8U,9U,31U,32U,UINT32_MAX}) {
        Port port;assert(!Port::resolve(d,port) && !port.buffer && !port.hpd);
    }
    for(auto pin:{0U,6U,7U,8U,9U,UINT32_MAX})assert(!gmbus(pin));
    // All connector assignments over the supported physical coordinates,
    // including DP-only, HDMI-only, mixed and multiple fixed DP routes.
    for(unsigned assignment=1;assignment<27;++assignment) {
        XELP::VBT::Ports fw;fw.status=XELP::VBT::Status::Valid;
        unsigned code=assignment,hdmi=0,dp=0;
        for(unsigned n=0;n<3;++n) {
            const unsigned kind=code%3;code/=3;if(!kind)continue;
            const unsigned d=physical[n];Port route;assert(Port::resolve(d,route));
            fw.present|=1U<<d;(kind==1?hdmi:dp)|=1U<<d;
            const uint32_t hdmiDVO[]={0,1,2},dpDVO[]={10,7,8};
            fw.port[d]={kind==1?0x60D2U:0x60D6U,kind==1?hdmiDVO[n]:dpDVO[n],n+1,kind==1?0:route.aux(),0xC0,0};
        }
        auto w=Wiring::fromFirmware(fw);assert(w.valid && w.hdmi==hdmi && w.dp==dp && !w.excluded);
        alignas(8) uint8_t platform[0x1C0]{};platform[0x12]=9;
        for(unsigned d=0;d<9;++d) {
            const uint32_t c[]={d,d>=3?d+6:d+1,1,d>=3?1U:0U,0x400,0x201};
            memcpy(platform+0x20+24*d,c,sizeof(c));
        }
        uint8_t saved[sizeof(platform)];memcpy(saved,platform,sizeof(saved));
        const unsigned last=fw.present&16?4:fw.present&8?3:0;
        platform[0x20+24*last+16]^=1;
        uint8_t bad[sizeof(platform)];memcpy(bad,platform,sizeof(bad));
        assert(!w.apply(platform,sizeof(platform)) && !memcmp(platform,bad,sizeof(bad)));
        memcpy(platform,saved,sizeof(saved));assert(w.apply(platform,sizeof(platform)));
        for(unsigned d=0;d<9;++d)if(fw.present&(1U<<d)) {
            auto c=reinterpret_cast<uint32_t *>(platform+0x20+24*d);
            assert(w.matches(c) && w.bound(platform,c));
            const bool isDP=dp&(1U<<d);
            assert(w.hasDP(d)==isDP && w.boundDP(platform,c)==isDP);
            assert(w.hotplugMode(true,c,platform,0x100,0x100,4)==1);
            assert(w.hotplugMode(false,c,platform,0x100,0x100,4)==4);
            assert(w.hotplugMode(true,c,platform,0x101,0x100,4)==4);
            assert(w.hotplugMode(true,c,platform,0,0,4)==4);
            for(unsigned mode=0;mode<4;++mode)assert(w.hotplugMode(true,c,platform,0x100,0x100,mode)==mode);
            uint32_t other[6];memcpy(other,c,sizeof(other));assert(!w.bound(platform,other));
            for(unsigned i=0;i<6;++i){c[i]^=1;assert(!w.matches(c));c[i]^=1;}
        }
        for(unsigned d=0;d<9;++d)if(!(fw.present&(1U<<d)))assert(!memcmp(platform+0x20+24*d,saved+0x20+24*d,24));
    }
    XELP::VBT::Ports fw;fw.status=XELP::VBT::Status::Valid;fw.present=8;
    fw.port[3]={0x60D6,7,0,0x10,0xC0,0};
    assert(Wiring::fromFirmware(fw).dp==8); // DP does not require GMBUS or HDMI
    for(unsigned bad=0;bad<8;++bad) {
        auto f=fw;
        if(bad==0)f.port[3].aux=0x20;
        if(bad==1)f.port[3].capabilities=1;
        if(bad==2)f.port[3].capabilities=2;
        if(bad==3)f.port[3].flags|=1;
        if(bad==4)f.port[3].type|=0x1000;
        if(bad==5)f.port[3].dvo=8;
        if(bad==6){f.present|=16;f.port[4]=f.port[3];}
        if(bad==7)f.status=XELP::VBT::Status::Conflict;
        assert(!Wiring::fromFirmware(f).dp);
    }
    fw.port[3]={0x60D2,1,5,0,0xC0,0};auto w=Wiring::fromFirmware(fw);
    assert(w.hdmi==8 && w.pin[3]==12 && w.usesDDC(12) && !w.usesDDC(9));
    fw.present|=16;fw.port[4]={0x60D2,2,5,0,0xC0,0};w=Wiring::fromFirmware(fw);
    assert(!w.selected() && w.excluded==24); // both ambiguous owners excluded
    fw={};fw.status=XELP::VBT::Status::Valid;fw.present=0x60;
    fw.port[5]={0x60D6,9,0,0x30,0xC0,0};fw.port[6]={0x60D2,4,5};
    w=Wiring::fromFirmware(fw);assert(w.valid && !w.selected() && w.excluded==0x60);
    // Physical TC1 still uses Combo B's clock selector, not DKL PLL1.
    Port hdmi;assert(Port::resolve(3,hdmi));
    assert(hdmi.clock==0x164280 && hdmi.clockShift==2 && hdmi.clockOff==0x800);
    // The 148.5 MHz firmware state, encoded from logical divisors rather than
    // copied into the implementation as a fixed display mode.
    const uint32_t params[]={464,2048,2,3,2};HDMIClock clock;
    assert(HDMIClock::encode(params,2,clock) && clock.cfg0==0x1001D0 && clock.cfg1==0xE84);
    assert(HDMIClock::encode(params,1,clock) && clock.cfg0==0x2001D0);
    for(unsigned word=0;word<5;++word) {
        uint32_t bad[5];memcpy(bad,params,sizeof(bad));bad[word]=UINT32_MAX;
        assert(!HDMIClock::encode(bad,2,clock) && !clock.cfg0 && !clock.cfg1);
    }
    assert(!HDMIClock::encode(nullptr,2,clock) && !HDMIClock::encode(params,3,clock));
    PLL pll;assert(PLL::resolve(2,pll) && pll.cfg0==0x164294 && pll.enable==0x46018);
    assert(PLL::resolve(3,pll) && pll.cfg0==0x1642C0 && pll.enable==0x46030);
    assert(!PLL::resolve(4,pll) && !pll.enable);
    uint32_t registers[0x128/4] {};registers[4]=0x64300;registers[(0x64C-0x580)/4]=0x4610C;
    uint32_t saved[0x128/4];memcpy(saved,registers,sizeof(saved));
    assert(comboRegisters(3,registers,sizeof(registers)/4));
    assert(registers[(0x5AC-0x580)/4]==0x64C04 && registers[(0x5B8-0x580)/4]==0x6C10C);
    assert(registers[(0x648-0x580)/4]==0x6C324 && registers[4]==0x64300);
    assert(!memcmp(registers,saved,0x5AC-0x580));
    assert(!memcmp(registers+(0x64C-0x580)/4,saved+(0x64C-0x580)/4,0x6A8-0x64C));
    assert(!comboRegisters(3,registers,sizeof(registers)/4)); // no repeated or foreign table
    memcpy(registers,saved,sizeof(saved));registers[4]=0x64400;
    assert(!comboRegisters(3,registers,sizeof(registers)/4));
    // Compose a firmware-qualified native panel with every external B/C
    // assignment. Panel remains kind0 and never gets external hotplug mode.
    for(unsigned assignment=0;assignment<9;++assignment) {
        XELP::VBT::Ports v;v.status=XELP::VBT::Status::Valid;v.present=1;
        v.port[0]={0x78C6,10,0,0x40,0,0};v.panel.valid=true;v.panel.ddi=0;
        v.panel.backlightValid=true;v.panel.pwmPresent=true;v.panel.backlightType=2;
        unsigned code=assignment;
        for(unsigned d:{3U,4U}){unsigned kind=code%3;code/=3;if(!kind)continue;
            v.present|=1U<<d;v.port[d]={kind==1?0x60D2U:0x60D6U,kind==1?d-2:d+4,d-1,kind==1?0:(d-2)*0x10,0,0};}
        auto panel=Wiring::fromFirmware(v);assert(panel.panel==1 && panel.selected()==v.present && !panel.excluded);
        alignas(8) uint8_t platform[0x1c0]{};platform[0x12]=9;
        const uint32_t nativePanel[]={0,0,0,0,2,0x18};memcpy(platform+0x20,nativePanel,sizeof(nativePanel));
        for(unsigned d:{3U,4U}){const uint32_t rec[]={d,d+6,1,1,0x400,0x201};memcpy(platform+0x20+24*d,rec,sizeof(rec));}
        for(unsigned mode:{1U,4U,~0U}) {
            *reinterpret_cast<uint32_t *>(platform+0x190)=mode;
            uint8_t saved[sizeof(platform)];memcpy(saved,platform,sizeof(saved));
            assert(!panel.apply(platform,sizeof(platform)) && !memcmp(saved,platform,sizeof(saved)));
        }
        for(unsigned mode:{0U,2U,3U}) {
            *reinterpret_cast<uint32_t *>(platform+0x190)=mode;
            assert(panel.apply(platform,sizeof(platform)) && !*reinterpret_cast<uint32_t *>(platform+0x190));
            // Restore the native external predecessors for the next application.
            for(unsigned d:{3U,4U}){const uint32_t rec[]={d,d+6,1,1,0x400,0x201};memcpy(platform+0x20+24*d,rec,sizeof(rec));}
        }
        assert(panel.apply(platform,sizeof(platform)));auto c=reinterpret_cast<uint32_t*>(platform+0x20);
        assert(panel.boundPanel(platform,c) && panel.boundSST(platform,c) && !panel.boundDP(platform,c));
        assert(!memcmp(c,nativePanel,sizeof(nativePanel)) && panel.hotplugMode(true,c,platform,1,1,4)==4);
        for(unsigned field=0;field<6;++field){c[field]^=1;assert(!panel.boundPanel(platform,c));c[field]^=1;}
        for(unsigned bad=0;bad<8;++bad){auto invalid=v;
            if(bad==0)invalid.panel.controller=1;if(bad==1)invalid.panel.backlightType=5;
            if(bad==2)invalid.panel.delays[0]=8192;if(bad==3)invalid.panel.delays[4]=30001;
            if(bad==4)invalid.panel.ddi=3;if(bad==5)invalid.port[0].aux=0x10;
            if(bad==6)invalid.unmapped=1;if(bad==7)invalid.panel.backlightValid=false;
            assert(!Wiring::fromFirmware(invalid).panel);
        }
    }
    puts("PASS ADL-S physical DDI/Combo PHY/HPD/PLL/DDC coordinates and ambiguous HDMI rejection");
}
