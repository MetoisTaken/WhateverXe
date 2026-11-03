#include "kern_alderlake_s.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace XELP;
using namespace XELP::VBT;
static void w16(std::vector<uint8_t> &b,size_t at,uint16_t v) {b[at]=v;b[at+1]=v>>8;}
static void w32(std::vector<uint8_t> &b,size_t at,uint32_t v) {w16(b,at,v);w16(b,at+2,v>>16);}
static std::vector<uint8_t> table(const std::vector<Port> &children) {
    std::vector<uint8_t> b(78+39*children.size());memcpy(b.data(),"$VBT",4);
    w16(b,22,48);w16(b,24,b.size());w32(b,28,48);
    memcpy(b.data()+48,"BIOS_DATA_BLOCK ",16);w16(b,64,250);w16(b,66,22);w16(b,68,b.size()-48);
    b[70]=2;w16(b,71,b.size()-73);b[77]=39;
    for(size_t n=0;n<children.size();++n) {
        const auto &p=children[n];const auto at=78+39*n;
        w16(b,at+2,p.type);b[at+16]=p.dvo;b[at+19]=p.ddc;b[at+25]=p.aux;
        b[at+23]=p.flags;b[at+33]=p.capabilities;
    }
    return b;
}
struct Platform {
    uint64_t words[0x1C0/8];
    uint8_t *data() {return reinterpret_cast<uint8_t *>(words);}
    uint32_t *config(unsigned d) {return reinterpret_cast<uint32_t *>(data()+0x20+24*d);}
    Platform() {
        memset(words,0x5A,sizeof(words));data()[0x12]=9;
        const uint32_t panel[]={0,0,0,0,2,0x18};memcpy(config(0),panel,24);
        for(unsigned d=1;d<9;++d) {
            const uint32_t record[]={d,d<3?d+1:d+6,1,d<3?0U:1U,0x400,0x201};
            memcpy(config(d),record,24);
        }
    }
};
int main() {
    // Recognized external routes include physical DP slots beyond the current
    // donor backend. NONE placeholders carry no connector despite their DVO.
    const std::vector<Port> children={
        {0,10},{0,23},{0,0},{0x60D2,1,2,0,0xC0,0},{0x60D2,2,3,0,0xC0,0},
        {0x60D6,9,4,0x30,0xC0,0},{0x60D6,11,5,0x50,0xC0,0},{0,15},{0,0}};
    const auto bytes=table(children);
    for(const auto family:{Family::AlderLakeS,Family::RaptorLakeS}) {
        const auto firmware=parse(bytes.data(),bytes.size(),family);
        assert(firmware.valid() && firmware.externalOnly && firmware.present==0x78 && !firmware.unmapped);
        assert(firmware.ambiguous==0x60 && !firmware.panel.valid);
        const auto wiring=AlderLakeS::Wiring::fromFirmware(firmware);
        assert(wiring.valid && wiring.hdmi==0x18 && !wiring.dp && wiring.excluded==0x60);
        assert(wiring.excludeDonorPanel && !wiring.panel);
        Platform output;const auto input=output;
        assert(wiring.apply(output.data(),sizeof(output)));
        assert(output.config(0)[4]==1 && !output.config(0)[5]);
        // Native identities, counts, pool sizes and unselected routes survive.
        auto expected=input;
        expected.config(0)[4]=1;expected.config(0)[5]=0;
        for(unsigned d:{3U,4U}) {
            expected.config(d)[2]=2;expected.config(d)[3]=0;
            expected.config(d)[4]=0x800;expected.config(d)[5]=0x202;
            assert(wiring.bound(output.data(),output.config(d)));
        }
        assert(!memcmp(output.data(),expected.data(),sizeof(output)));
        for(unsigned d=0;d<9;++d)assert(output.config(d)[0]==d);
        // Every guard runs before the first write, including both the donor
        // panel and unselected native identities.
        for(unsigned d=0;d<9;++d) {
            auto broken=input;broken.config(d)[0]=9;const auto keep=broken;
            assert(!wiring.apply(broken.data(),sizeof(broken)));
            assert(!memcmp(broken.data(),keep.words,sizeof(broken)));
        }
        for(unsigned field=1;field<6;++field) {
            auto broken=input;++broken.config(0)[field];const auto keep=broken;
            assert(!wiring.apply(broken.data(),sizeof(broken)));
            assert(!memcmp(broken.data(),keep.words,sizeof(broken)));
        }
        auto broken=input;broken.config(4)[2]=2;const auto keep=broken;
        assert(!wiring.apply(broken.data(),sizeof(broken)) && !memcmp(broken.data(),keep.words,sizeof(broken)));
        // Unknown, nondigital internal, digital internal, unmapped and
        // malformed children at every inventory position cannot remove A.
        for(unsigned slot=0;slot<children.size();++slot)
        for(const auto bad:{Port{0x100,0},Port{0x1000,0},Port{0x1022,4},Port{0x78C6,10},
                            Port{0x60D2,255},Port{0x8002,0},Port{2,0}}) {
            auto altered=children;altered[slot]=bad;const auto b=table(altered);
            const auto f=parse(b.data(),b.size(),family);
            assert(!f.externalOnly);
            const auto w=AlderLakeS::Wiring::fromFirmware(f);assert(!w.excludeDonorPanel);
            if(w.valid && w.selected()) {
                Platform out;assert(w.apply(out.data(),sizeof(out)));
                assert(!memcmp(out.config(0),input.words+4,24));
            }
        }
        // Incomplete/empty inventories and duplicate DDI identities never certify absence.
        for(size_t length=0;length<bytes.size();++length)
            assert(!parse(bytes.data(),length,family).externalOnly);
        for(const auto list:{std::vector<Port>{},std::vector<Port>{{0,23}},
                            std::vector<Port>{{0x60D2,1,2},{0x60D6,7,0,0x10}}}) {
            const auto b=table(list);assert(!parse(b.data(),b.size(),family).externalOnly);
        }
        for(unsigned stride:{0U,37U,40U,65U}) {
            auto b=bytes;b[77]=stride;assert(!parse(b.data(),b.size(),family).externalOnly);
        }
        for(unsigned version:{215U,257U}) {
            auto b=bytes;w16(b,64,version);assert(!parse(b.data(),b.size(),family).externalOnly);
        }
        for(unsigned count:{0U,4U,5U+39U*8U+38U}) {
            auto b=bytes;w16(b,71,count);assert(!parse(b.data(),b.size(),family).externalOnly);
        }
        auto missing=bytes;missing[70]=1;assert(!parse(missing.data(),missing.size(),family).externalOnly);
        auto cut=bytes;cut.pop_back();w16(cut,24,cut.size());w16(cut,68,cut.size()-48);
        assert(!parse(cut.data(),cut.size(),family).externalOnly);
        // Missing timing/power facts do not mean the physical panel is absent.
        auto panelChildren=children;panelChildren[0]={0x78C6,10,0,0x40};
        const auto panelBytes=table(panelChildren);const auto panel=parse(panelBytes.data(),panelBytes.size(),family);
        assert(panel.valid() && !panel.panel.valid && !panel.externalOnly);
        const auto panelWiring=AlderLakeS::Wiring::fromFirmware(panel);
        Platform retained;assert(panelWiring.apply(retained.data(),sizeof(retained)));
        assert(!memcmp(retained.config(0),input.words+4,24));
        // A valid external A route is still selected normally and never
        // converted to a dummy connector by this absence-only correction.
        auto externalA=children;externalA[0]={0x60D2,0,1};
        const auto aBytes=table(externalA);const auto a=parse(aBytes.data(),aBytes.size(),family);
        const auto aw=AlderLakeS::Wiring::fromFirmware(a);
        assert(a.externalOnly && !aw.excludeDonorPanel && (aw.hdmi&1));
        Platform aOut;assert(aw.apply(aOut.data(),sizeof(aOut)));
        assert(aOut.config(0)[4]==0x800 && aOut.config(0)[5]==0x202);
        // Preserve existing connector admission with a stale checksum or an
        // unrelated malformed tail; this certificate covers only complete GD.
        auto tail=bytes;tail.push_back(58);tail.push_back(2);tail.push_back(0);tail.push_back(0);
        w16(tail,24,tail.size());w16(tail,68,tail.size()-48);tail[26]=0x5A;
        const auto t=parse(tail.data(),tail.size(),family);assert(t.valid() && t.externalOnly);
        auto uncertain=firmware;uncertain.externalOnly=false;
        assert(!AlderLakeS::Wiring::fromFirmware(uncertain).excludeDonorPanel);
        uncertain=firmware;uncertain.panel.valid=true;
        assert(!AlderLakeS::Wiring::fromFirmware(uncertain).excludeDonorPanel);
        uncertain=firmware;++uncertain.unmapped;
        assert(!AlderLakeS::Wiring::fromFirmware(uncertain).excludeDonorPanel);
    }
    puts("PASS ADL-S/RPL-S complete external inventory, exact donor panel exclusion, identity/atomicity and unknown/internal preservation");
}
