//
//  kern_mps_intel.hpp
//  WhateverXe
//
//  MPSCore (macOS 26) classifies an Intel GPU by CPUID.7.EBX.SGX. Without
//  SGX (Alder Lake and Raptor Lake) it builds the type 0x18 device, for
//  which MPSNDArrayMatrixMultiplication never creates its MPSMatrix helper:
//  every NDArray/MPSGraph matrix multiply is a silent no-op. Take the SGX
//  branch (device type 0x19) instead.
//
//  MPSCore lives in the dyld shared cache, which Lilu's user patcher does not
//  handle since Big Sur. The patch is applied to the shared physical page from
//  the context of a process creating its GPU task, after verifying every
//  byte, and re-checked on each later task in case the clean page was reloaded.
//  Opt out with -wex-mps-intel-off.
//

#ifndef kern_mps_intel_hpp
#define kern_mps_intel_hpp

#include <stdint.h>
#include <string.h>

namespace MPSIntel {

static constexpr const char *MapPath =
	"/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h.map";
static constexpr const char *CorePath =
	"/System/Library/Frameworks/MetalPerformanceShaders.framework/Versions/A/Frameworks/MPSCore.framework/Versions/A/MPSCore";

// MPSDevice::GetMPSDevice: cpuid(7,0); calloc(1,0xAC0); test bl,4; jne <type 0x19>
static constexpr uint8_t Find[] {
	0xB8,0x07,0x00,0x00,0x00, 0x31,0xC9, 0x0F,0xA2,
	0xBF,0x01,0x00,0x00,0x00, 0xBE,0xC0,0x0A,0x00,0x00, 0xBA,0x85,0xC3,0x21,0x3F,
	0xE8,0x85,0x47,0x01,0x00,
	0xF6,0xC3,0x04, 0x0F,0x85,0xF4,0x01,0x00,0x00
};
// The conditional jump becomes nop; jmp rel32 with the same displacement.
static constexpr uint32_t JumpOffset = 32;
static constexpr uint8_t Original[2] {0x0F,0x85}, Patched[2] {0x90,0xE9};

// dyld_all_image_infos::sharedCacheSlide (LP64).
static constexpr uint32_t SharedCacheSlideOffset = 152;

static inline bool cpuHasSGX() {
#if defined(__x86_64__)
	uint32_t a=7,b,c=0,d;
	asm volatile("cpuid" : "+a"(a),"=b"(b),"+c"(c),"=d"(d));
	return (b&4)!=0;
#else
	return true; // host-side tests: never patch
#endif
}

static inline uint64_t parseHex(const char *&p,const char *end) {
	while (p<end && (*p==' ' || *p=='\t')) ++p;
	if (p+1<end && p[0]=='0' && (p[1]=='x' || p[1]=='X')) p+=2;
	uint64_t v=0;
	for (;p<end;++p) {
		const char c=*p;uint32_t n;
		if (c>='0' && c<='9') n=static_cast<uint32_t>(c-'0');
		else if (c>='a' && c<='f') n=static_cast<uint32_t>(c-'a'+10);
		else if (c>='A' && c<='F') n=static_cast<uint32_t>(c-'A'+10);
		else break;
		v=(v<<4)|n;
	}
	return v;
}

// Find the unslid __TEXT range of MPSCore in the dyld cache map text.
static inline bool textRange(const char *map,size_t size,uint64_t &start,uint64_t &end) {
	const size_t n=strlen(CorePath);const char *stop=map+size;
	for (const char *p=map;p+n<stop;++p) {
		if ((p==map || p[-1]=='\n') && !memcmp(p,CorePath,n) && p[n]=='\n') {
			const char *q=p+n+1;const char *line=q;
			while (q<stop && *q!='\n') ++q;
			const char *t=line;
			while (t+6<q && memcmp(t,"__TEXT",6)) ++t;
			if (t+6>=q) return false;
			t+=6;start=parseHex(t,q);
			while (t<q && *t!='>') ++t;
			if (t>=q) return false;
			++t;end=parseHex(t,q);
			return start && end>start;
		}
	}
	return false;
}

}

#endif /* kern_mps_intel_hpp */
