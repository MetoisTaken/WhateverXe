#ifndef kern_rpls_outgoing_hpp
#define kern_rpls_outgoing_hpp
#include <stdint.h>
#include "kern_tgl_csb.hpp"

namespace RPLSOutgoing {
constexpr unsigned Words=64, EventWords=24, Capacity=64;
enum Word {
 Schema, Status, Begin, End, Owner, Streamer, Epoch, Consumed, Raw, Source,
 Active, Item, Context, Descriptor, Scheduling, LRCGPU, LRCClass, LRCBuffer,
 LRCCPU, LRCLength, LRCCopied, Ring, RingClass, RingBuffer, RingCPU,
 RingLength, RingCopied, POSHBuffer, POSHCPUReserved, POSHExpectedWatchLength, RingGPU,
 Head, Tail, Control, PDP, POSHGPU, CumulativeLowDWORD, RestoreFinished, SaveFinished,
 SchedulingAfter, DescriptorAfter, Stable, ContextClass, ContextCheck, OwnerFlags
};
struct Snapshot {uint64_t q[Words] {};uint8_t lrc[8192] {},ring[4096] {};};
struct Event {uint64_t q[EventWords] {};uint32_t ready {};};
inline bool range(uint64_t p,uint64_t n) {
 return n && (p>>47)==0x1FFFF && p<=UINT64_MAX-n;
}
inline bool gpuRange(uint64_t p,uint64_t n) {
 return p && n && !(p&4095) && !(n&4095) && p<0x100000000ULL && n<=0x100000000ULL-p;
}
inline bool overlaps(uint64_t a,uint64_t n,uint64_t b,uint64_t m) {
 return gpuRange(a,n) && gpuRange(b,m) && a<b+m && b<a+n;
}
inline bool selected(const TGLCSB::Event &e) {
 return e.raw==0x0000800003FF8000ULL && e.kind==TGLCSB::Kind::ActiveToIdleComplete && e.away.id==1 && !e.away.counter &&
  e.to.id==0x7FF && !e.to.counter && !e.engineClass && !e.engineInstance &&
  !e.detail && !e.newQueue && !e.semaphoreWaitMode;
}
struct Metadata {
 uint64_t active {},item {},context {},descriptor {},scheduling {},contextClass {},check {},
  lrc {},lrcClass {},lrcCPU {},lrcLength {},ring {},ringClass {},ringBuffer {},ringCPU {},
  ringLength {},posh {},flags {};
};
template<class Read>
bool metadata(Metadata &m,uint64_t streamer,uint64_t owner,uint64_t bufferClass,
              uint64_t ringClass,uint64_t contextCheck,Read read) {
 auto get=[&](uint64_t p,uint64_t off,uint64_t &v) {
  return range(p,off+8) && read(p+off,v);
 };
 uint64_t v=0,second=0;
 if(!get(streamer,0x10,v) || v!=owner || !get(owner,0x1190,m.flags) || (m.flags&0x1000) ||
  !get(streamer,0x50,m.active) || (m.active&=UINT32_MAX)>1 ||
  !get(streamer,0x30+m.active*16,m.item) || !m.item ||
  !get(streamer,0x38+m.active*16,second) || second || !get(m.item,0,m.context) ||
  !get(m.context,0,m.contextClass) || !get(m.contextClass,0x128,m.check) || m.check!=contextCheck ||
  !get(m.context,0x50,v) || v!=owner || !get(m.context,0x7C,v) || uint32_t(v)!=1 ||
  !get(m.context,0x89,m.descriptor) || m.descriptor!=0x20402B8319ULL ||
  !get(m.context,0x60,m.scheduling) || !((m.scheduling>>16)&0xFFFF) ||
  !get(m.context,0x98,m.lrc) || !get(m.context,0xB0,m.ring) || !get(m.context,0xA0,m.posh) ||
  !get(m.lrc,0,m.lrcClass) || m.lrcClass!=bufferClass ||
  !get(m.lrc,0x10,v) || v!=owner || !get(m.lrc,0x20,m.lrcLength) || m.lrcLength!=81920 ||
  !get(m.lrc,0x38,m.lrcCPU) || (m.lrcCPU&4095) || !range(m.lrcCPU,8192) ||
  !get(m.ring,0,m.ringClass) || m.ringClass!=ringClass ||
  !get(m.ring,0x10,v) || v!=owner || !get(m.ring,0x20,v) || v!=m.context ||
  !get(m.ring,0x40,v) || uint8_t(v) ||
  !get(m.ring,0x8C,m.ringLength) || uint32_t(m.ringLength)!=32768 ||
  !get(m.ring,0x80,m.ringBuffer) || !get(m.ringBuffer,0,v) || v!=bufferClass ||
  !get(m.ringBuffer,0x10,v) || v!=owner ||
  !get(m.ringBuffer,0x20,v) || v!=32768 || !get(m.ringBuffer,0x38,m.ringCPU) ||
  (m.ringCPU&4095) || !range(m.ringCPU,4096))return false;
 m.ringLength=uint32_t(m.ringLength);return true;
}
inline bool same(const Metadata &a,const Metadata &b) {
 // Scheduling may change for another native queue; its execution pin must
 // stay positive. Identity, ownership, allocation and CPU-map lineage may not.
 return a.active==b.active && a.item==b.item && a.context==b.context &&
  a.descriptor==b.descriptor && ((b.scheduling>>16)&0xFFFF) && a.contextClass==b.contextClass &&
  a.check==b.check && a.lrc==b.lrc && a.lrcClass==b.lrcClass && a.lrcCPU==b.lrcCPU &&
  a.lrcLength==b.lrcLength && a.ring==b.ring && a.ringClass==b.ringClass &&
  a.ringBuffer==b.ringBuffer && a.ringCPU==b.ringCPU && a.ringLength==b.ringLength &&
  a.posh==b.posh;
}
template<class Read,class Copy,class Guard>
void capture(Snapshot &r,uint64_t streamer,uint64_t owner,uint64_t bufferClass,
 uint64_t ringClass,uint64_t contextCheck,Read read,Copy copy,Guard guard) {
 auto q=r.q;q[Schema]=1;q[Owner]=owner;q[Streamer]=streamer;q[Status]=1;
 if(!guard())return;
 Metadata a,b;
 q[Status]=2;if(!metadata(a,streamer,owner,bufferClass,ringClass,contextCheck,read))return;
 q[Active]=a.active;q[Item]=a.item;q[Context]=a.context;q[Descriptor]=a.descriptor;
 q[Scheduling]=a.scheduling;q[LRCGPU]=(a.descriptor>>12&0xFFFFF)*4096;
 q[LRCClass]=a.lrcClass;q[LRCBuffer]=a.lrc;q[LRCCPU]=a.lrcCPU;q[LRCLength]=a.lrcLength;
 q[Ring]=a.ring;q[RingClass]=a.ringClass;q[RingBuffer]=a.ringBuffer;q[RingCPU]=a.ringCPU;
 q[RingLength]=a.ringLength;q[POSHBuffer]=a.posh;
 // The context's POSH member is a historical scalar only. Its lifetime is
 // not part of the LRC/ring pin proof; never dereference it here. Watch the
 // image-derived GPU range using the expected four-page extent only.
 q[POSHExpectedWatchLength]=16384;
 q[ContextClass]=a.contextClass;q[ContextCheck]=a.check;q[OwnerFlags]=a.flags;
 q[Status]=3;q[LRCCopied]=copy(a.lrcCPU,r.lrc,sizeof(r.lrc));
 if(q[LRCCopied]!=sizeof(r.lrc))return;
 q[RingCopied]=copy(a.ringCPU,r.ring,sizeof(r.ring));if(q[RingCopied]!=sizeof(r.ring))return;
 auto u32=[&](unsigned o)->uint64_t {
  const auto p=r.lrc+o;return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
 };
 auto u64=[&](unsigned o)->uint64_t {return u32(o)|(u32(o+4)<<32);};
 q[Head]=u32(0x1014);q[Tail]=u32(0x101C);q[RingGPU]=u32(0x1024);q[Control]=u32(0x102C);
 q[PDP]=(u32(0x10C4)<<32)|u32(0x10CC);q[POSHGPU]=u32(0x10D8)&~4095ULL;
 q[CumulativeLowDWORD]=u32(0x40);q[RestoreFinished]=u64(0x50);q[SaveFinished]=u64(0x58);
 q[Status]=4;
 if(!guard() || !metadata(b,streamer,owner,bufferClass,ringClass,contextCheck,read) || !same(a,b))return;
 q[SchedulingAfter]=b.scheduling;q[DescriptorAfter]=b.descriptor;q[Stable]=1;q[Status]=0;
}
inline uint64_t watched(const Snapshot &s,uint64_t start,uint64_t length) {
 if(s.q[Status] || !s.q[Stable])return 0;
 return (overlaps(start,length,s.q[LRCGPU],s.q[LRCLength])?1:0) |
  (overlaps(start,length,s.q[RingGPU],s.q[RingLength])?2:0) |
  (overlaps(start,length,s.q[POSHGPU],s.q[POSHExpectedWatchLength])?4:0);
}
}
#endif
