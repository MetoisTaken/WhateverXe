//
//  kern_hdmi_link.hpp
//  WhateverXe
//
//  HDMI sink capabilities from EDID CTA extensions and the TMDS link plan
//  (deep-colour clock, scrambling) for native HDMI combo routes.
//

#ifndef kern_hdmi_link_hpp
#define kern_hdmi_link_hpp

#include <stdint.h>

namespace HDMILink {

// Without SCDC scrambling a TMDS character rate above 340 MHz is not allowed.
static constexpr uint32_t ScramblingThresholdKHz=340000;
// Highest TMDS character rate of the combo PHY HDMI 2.0 path.
static constexpr uint32_t SourceMaxTMDSKHz=600000;

struct Sink {
    bool valid {};          // a CTA extension with an HDMI VSDB was seen
    bool scdc {};           // HF-VSDB SCDC_Present
    bool deep30 {},deep36 {};
    uint32_t maxTMDSKHz {}; // HF-VSDB Max_TMDS_Character_Rate, 0 when absent

    // Highest TMDS character rate both ends support.
    uint32_t linkMaxKHz() const {
        if(!valid || !scdc || maxTMDSKHz<=ScramblingThresholdKHz)return ScramblingThresholdKHz;
        return maxTMDSKHz<SourceMaxTMDSKHz ? maxTMDSKHz : SourceMaxTMDSKHz;
    }
    bool supportsBitsPerPixel(uint32_t bpp) const {
        return bpp<=24 || (valid && ((bpp==30 && deep30) || (bpp==36 && deep36)));
    }
};

static inline bool checksum(const uint8_t *b) {
    uint8_t sum=0;for(unsigned i=0;i<128;++i)sum+=b[i];
    return sum==0;
}

static inline bool isBaseBlock(const uint8_t *b) {
    static const uint8_t header[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0};
    for(unsigned i=0;i<8;++i)if(b[i]!=header[i])return false;
    return true;
}

// Parse one 128-byte CTA-861 extension. Returns false when the block is not a
// valid CTA extension; the sink is then left unchanged.
static inline bool parseCTA(const uint8_t *b,Sink &sink) {
    if(b[0]!=0x02 || b[1]<3 || !checksum(b))return false;
    // Byte 2 is where detailed timings start; data blocks lie in [4, end).
    const unsigned end=b[2];
    if(end>127)return false;
    Sink found {};
    for(unsigned i=4;i<end;) {
        const unsigned tag=b[i]>>5,length=b[i]&0x1F;
        if(i+1+length>end)break;
        const uint8_t *d=b+i+1;
        if(tag==3 && length>=3) {
            if(d[0]==0x03 && d[1]==0x0C && d[2]==0x00) {
                found.valid=true;
                if(length>=6) {found.deep30=(d[5]&0x10)!=0;found.deep36=(d[5]&0x20)!=0;}
            } else if(d[0]==0xD8 && d[1]==0x5D && d[2]==0xC4 && length>=6) {
                found.maxTMDSKHz=d[4]*5000U;
                found.scdc=(d[5]&0x80)!=0;
            }
        }
        i+=1+length;
    }
    if(!found.valid)found.scdc=false,found.maxTMDSKHz=0;
    sink=found;
    return true;
}

// EDID 1.3 cannot state a colour depth, so macOS treats such HDMI sinks as
// 8 bpc. Present the HDMI VSDB DC_30bit capability as an EDID 1.4 digital
// HDMI-a input with 10 bits per primary. Only an undefined 1.3 digital input
// is changed; returns true when the block was rewritten.
// Feature byte 0x18 bits 4:3 are the display type in 1.3 (01 = RGB colour) but
// the supported colour encodings in 1.4 (01 = RGB + YCbCr 4:4:4). Keep them at
// RGB 4:4:4 only: a reinterpreted 01 makes macOS drive HDMI as YCbCr, whose
// output CSC the donor programs wrongly, and the sink shows solid green.
static inline bool upgradeBaseDepth(uint8_t *b,const Sink &sink) {
    if(!isBaseBlock(b) || !checksum(b) || b[0x12]!=1 || b[0x13]!=3 || b[0x14]!=0x80 || !sink.valid || !sink.deep30)return false;
    b[0x13]=4;b[0x14]=0xB2;b[0x18]&=static_cast<uint8_t>(~0x18U);
    uint8_t sum=0;for(unsigned i=0;i<127;++i)sum+=b[i];
    b[127]=static_cast<uint8_t>(0x100-sum);
    return true;
}

// CTA-861 extension byte 3 bits 5:4 advertise YCbCr 4:4:4 / 4:2:2. Once the
// base block is EDID 1.4, macOS honours them and drives HDMI as YCbCr, which
// this donor's output CSC cannot produce. Present the sink as RGB only.
static inline bool stripYCbCr(uint8_t *b) {
    if(!b || b[0]!=0x02 || b[1]<1 || !checksum(b) || !(b[3]&0x30))return false;
    b[3]=static_cast<uint8_t>(b[3]&~0x30U);
    uint8_t sum=0;for(unsigned i=0;i<127;++i)sum+=b[i];
    b[127]=static_cast<uint8_t>(0x100-sum);
    return true;
}

struct Plan {
    uint32_t bitsPerPixel {24};
    uint32_t tmdsKHz {};
    bool scrambling {};
    bool clamped {};
};

// The TMDS character rate is pixel rate x bpp/24. Fall back to 8 bpc when the
// requested depth is unsupported by the sink or exceeds the link budget.
static inline Plan plan(const Sink &sink,uint64_t pixelHz,uint32_t bpp) {
    Plan p {};
    const uint64_t pixelKHz=pixelHz/1000;
    const uint32_t limit=sink.linkMaxKHz();
    p.bitsPerPixel=bpp==30 || bpp==36 ? bpp : 24;
    if(p.bitsPerPixel>24 && (!sink.supportsBitsPerPixel(p.bitsPerPixel) || pixelKHz*p.bitsPerPixel/24>limit)) {
        p.bitsPerPixel=24;p.clamped=true;
    }
    const uint64_t tmds=pixelKHz*p.bitsPerPixel/24;
    p.tmdsKHz=tmds>UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(tmds);
    p.scrambling=p.tmdsKHz>ScramblingThresholdKHz;
    return p;
}

// SCDC TMDS_Config (offset 0x20): bit 1 TMDS bit clock ratio 1/40, bit 0 scrambling.
static inline uint8_t scdcConfig(const Plan &p) {return p.scrambling ? 0x03 : 0x00;}

// TRANS_DDI_FUNC_CTL: bit 4 high TMDS character rate, bit 0 HDMI scrambling.
static constexpr uint32_t TranscoderScramblingBits=0x11;
// TVIDEO_DIP_GCP colour indication and VIDEO_DIP_CTL GCP enable.
static constexpr uint32_t GCPColourIndication=0x4,DIPEnableGCP=1U<<16;

}

#endif /* kern_hdmi_link_hpp */
