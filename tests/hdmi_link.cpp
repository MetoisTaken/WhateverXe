#include "kern_hdmi_link.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace HDMILink;

// CTA extension of a sink with HDMI 1.4 VSDB (DC_30/DC_36, 300 MHz) and an
// HF-VSDB announcing 600 MHz with SCDC present.
static void makeCTA(uint8_t *b,bool hf,bool scdc,uint8_t maxTMDS,uint8_t deep) {
    memset(b,0,128);
    b[0]=0x02;b[1]=0x03;b[3]=0xF0;
    unsigned i=4;
    const uint8_t vsdb[]={0x67,0x03,0x0C,0x00,0x20,0x00,deep,0x3C};
    memcpy(b+i,vsdb,sizeof(vsdb));i+=sizeof(vsdb);
    if(hf) {
        const uint8_t hfvsdb[]={0x66,0xD8,0x5D,0xC4,0x01,maxTMDS,static_cast<uint8_t>(scdc?0x80:0)};
        memcpy(b+i,hfvsdb,sizeof(hfvsdb));i+=sizeof(hfvsdb);
    }
    b[2]=static_cast<uint8_t>(i);
    uint8_t sum=0;for(unsigned j=0;j<127;++j)sum+=b[j];
    b[127]=static_cast<uint8_t>(0x100-sum);
}

int main() {
    uint8_t b[128];
    Sink s {};
    makeCTA(b,true,true,0x78,0xB8);
    assert(parseCTA(b,s) && s.valid && s.scdc && s.deep30 && s.deep36 && s.maxTMDSKHz==600000);
    {uint8_t c[128];memcpy(c,b,128);assert(c[3]==0xF0 && stripYCbCr(c) && c[3]==0xC0 && checksum(c));
     for(unsigned i=0;i<127;++i)if(i!=3)assert(c[i]==b[i]);
     assert(!stripYCbCr(c));Sink again {};assert(parseCTA(c,again) && again.deep30 && again.maxTMDSKHz==600000);}
    assert(s.linkMaxKHz()==600000);

    // 1080p120 and 4K30 (297 MHz) at 10 bpc need 371.25 MHz and scrambling.
    auto p=plan(s,297000000,30);
    assert(p.bitsPerPixel==30 && p.tmdsKHz==371250 && p.scrambling && !p.clamped && scdcConfig(p)==3);
    // 1080p60 at 10 bpc stays below the scrambling threshold.
    p=plan(s,148500000,30);
    assert(p.bitsPerPixel==30 && p.tmdsKHz==185625 && !p.scrambling && scdcConfig(p)==0);
    // 4K60 at 10 bpc exceeds 600 MHz: fall back to 8 bpc with scrambling.
    p=plan(s,594000000,30);
    assert(p.bitsPerPixel==24 && p.clamped && p.tmdsKHz==594000 && p.scrambling);
    // 8 bpc requests are never changed.
    p=plan(s,297000000,24);
    assert(p.bitsPerPixel==24 && !p.clamped && p.tmdsKHz==297000 && !p.scrambling);

    // No SCDC: the budget is 340 MHz.
    makeCTA(b,true,false,0x78,0xB8);
    assert(parseCTA(b,s) && !s.scdc && s.linkMaxKHz()==340000);
    p=plan(s,297000000,30);
    assert(p.bitsPerPixel==24 && p.clamped && !p.scrambling);
    p=plan(s,148500000,30);
    assert(p.bitsPerPixel==30 && !p.clamped);

    // No deep-colour bits: 10 bpc falls back to 8 bpc.
    makeCTA(b,true,true,0x78,0x00);
    assert(parseCTA(b,s) && !s.deep30);
    p=plan(s,148500000,30);
    assert(p.bitsPerPixel==24 && p.clamped);

    // HF-VSDB without an HDMI 1.4 VSDB is not an HDMI sink.
    makeCTA(b,true,true,0x78,0xB8);b[4+1]=0x01; // corrupt the VSDB OUI
    uint8_t sum=0;for(unsigned j=0;j<127;++j)sum+=b[j];b[127]=static_cast<uint8_t>(0x100-sum);
    assert(parseCTA(b,s) && !s.valid && !s.scdc && s.linkMaxKHz()==340000);

    // Invalid checksum or a non-CTA block leaves the sink unchanged.
    Sink keep {};keep.valid=keep.scdc=true;keep.maxTMDSKHz=600000;
    makeCTA(b,true,true,0x78,0xB8);b[127]^=1;
    assert(!parseCTA(b,keep) && keep.scdc);
    b[0]=0x10;assert(!parseCTA(b,keep));

    // The real Odyssey G8 extension.
    static const char hex[]="02035bf0e278034b616076101f04135f3f03122309070783010000e2004fe305c0006b030c002000b83c200020016dd85dc40178805b023078c354056d1a0000020f307800048b065a0ae60605018b5a00e30f0700e5018b849079565e00a0a0a0295030203500b9882100001a6fc200a0a0a0555030203500b9882100001a26";
    for(unsigned i=0;i<128;++i){unsigned v;sscanf(hex+2*i,"%2x",&v);b[i]=static_cast<uint8_t>(v);}
    assert(parseCTA(b,s) && s.valid && s.scdc && s.deep30 && s.deep36 && s.maxTMDSKHz==600000);
    {uint8_t c[128];memcpy(c,b,128);assert(c[3]==0xF0 && stripYCbCr(c) && c[3]==0xC0 && checksum(c));
     for(unsigned i=0;i<127;++i)if(i!=3)assert(c[i]==b[i]);
     assert(!stripYCbCr(c));Sink again {};assert(parseCTA(c,again) && again.deep30 && again.maxTMDSKHz==600000);}
    uint8_t base[128]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0};assert(isBaseBlock(base) && !isBaseBlock(b));

    // The real Odyssey G8 base block: EDID 1.3, digital input, depth undefined.
    static const char baseHex[]="00ffffffffffff004c2dec71584c4e3029210103804628782a4ed5ae4e45aa270e5054210800010181c0810081809500a9c0b300d1c008e80030f2705a80b0588a00b9882100001e000000fd001e781eff77000a202020202020000000fc004f6479737365792047380a2020000000ff00484e42574130303035370a2020012f";
    for(unsigned i=0;i<128;++i){unsigned v;sscanf(baseHex+2*i,"%2x",&v);base[i]=static_cast<uint8_t>(v);}
    assert(checksum(base));
    uint8_t copy[128];memcpy(copy,base,128);
    assert(base[0x18]==0x2A); // 1.3 "RGB colour display" = 1.4 "RGB + YCbCr 4:4:4"
    assert(upgradeBaseDepth(copy,s) && copy[0x13]==4 && copy[0x14]==0xB2 && checksum(copy));
    assert(copy[0x18]==0x22); // 1.4: RGB 4:4:4 only, so macOS keeps HDMI in RGB
    for(unsigned i=0;i<127;++i)if(i!=0x13 && i!=0x14 && i!=0x18)assert(copy[i]==base[i]);
    assert(!upgradeBaseDepth(copy,s)); // already 1.4
    Sink shallow=s;shallow.deep30=false;memcpy(copy,base,128);
    assert(!upgradeBaseDepth(copy,shallow) && !memcmp(copy,base,128));
    Sink unknown {};assert(!upgradeBaseDepth(copy,unknown));
    copy[0x14]=0x0E;copy[127]=static_cast<uint8_t>(copy[127]+0x80-0x0E);assert(checksum(copy) && !upgradeBaseDepth(copy,s)); // analogue input
    puts("PASS HDMI sink parsing and TMDS plan: deep colour, 340 MHz scrambling threshold, 600 MHz budget and 8 bpc fallback");
}
