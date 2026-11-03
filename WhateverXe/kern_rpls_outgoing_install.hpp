#ifndef kern_rpls_outgoing_install_hpp
#define kern_rpls_outgoing_install_hpp
#include <stdint.h>
#include "kern_rpls_outgoing.hpp"
namespace RPLSOutgoingInstall {
constexpr unsigned Segments=8;
struct Layout {uint64_t q[16] {};uint64_t segment[Segments][8] {};};
struct Record {uint64_t q[16] {};Layout layout;uint64_t symbol[6][10] {},route[3][20] {};};
static_assert(sizeof(Record)==1728,"fixed CPU installation receipt");
constexpr uint64_t name(const char *p) {
 uint64_t v=0;for(unsigned i=0;i<8 && p[i];++i)v|=uint64_t(uint8_t(p[i]))<<(i*8);return v;
}
// Only the owned image's first header page is read. A callback span may include
// unmapped gaps between KC segments, so it is never treated as a copy extent.
template<class Read>
bool inspect(Layout &l,uint64_t header,uint64_t callbackSize,Read read) {
 auto q=l.q;q[0]=1;q[1]=1;q[2]=header;q[3]=callbackSize;
 if(callbackSize<4096 || !RPLSOutgoing::range(header,4096))return false;
 uint64_t h=0,commands=0;
 if(!read(header,h) || !read(header+16,commands))return false;
 q[4]=uint32_t(h);q[5]=h>>32;q[6]=uint32_t(commands);q[7]=commands>>32;
 q[1]=2;if(q[4]!=0xFEEDFACF || q[5]!=0x01000007)return false;
 q[1]=3;if(!q[6] || q[6]>64 || q[7]>4096-32 || q[7]<q[6]*8)return false;
 uint64_t offset=32;const auto end=32+q[7];
 for(unsigned i=0;i<q[6];++i) {
  q[1]=4;
  if(offset>end || end-offset<8 || !read(header+offset,h))return false;
  const auto command=uint32_t(h);const auto length=h>>32;
  if(length<8 || (length&7) || length>end-offset)return false;
  if(command==0x19) {
   q[1]=5;if(length<72 || q[9]>=Segments)return false;
   auto s=l.segment[q[9]++];
   if(!read(header+offset+8,s[0]) || !read(header+offset+16,s[1]) ||
    !read(header+offset+24,s[2]) || !read(header+offset+32,s[3]) ||
    !read(header+offset+40,s[4]) || !read(header+offset+48,s[5]) ||
    !read(header+offset+56,h))return false;
   s[6]=uint32_t(h);s[7]=h>>32;
   if(s[3]>UINT64_MAX-s[2])return false;
   if(s[0]==name("__TEXT") && !s[1] && s[3]>=4096 && (s[7]&5)==5) {
    q[10]|=1ULL<<(q[9]-1);q[15]=header-s[2];
   }
   if(s[0]==name("__DATA") && !s[1] && (s[7]&1))q[11]|=1ULL<<(q[9]-1);
  } else if(command==0x1B) {
   q[1]=6;if(length!=24 || q[14] || !read(header+offset+8,q[12]) || !read(header+offset+16,q[13]))return false;
   q[14]=1;
  }
  offset+=length;++q[8];
 }
 q[1]=7;
 if(offset!=end || !q[10] || (q[10]&(q[10]-1)) || !q[11] || (q[11]&(q[11]-1)) || !q[14])return false;
 q[1]=0;return true;
}
inline bool dataClass(const Layout &l,uint64_t symbol) {
 if(l.q[1] || !RPLSOutgoing::range(symbol,0xA8) || (symbol&7))return false;
 for(unsigned i=0;i<l.q[9] && i<Segments;++i) {
  const auto s=l.segment[i];
  if((l.q[11]&(1ULL<<i)) && RPLSOutgoing::range(s[2],s[3]) && symbol>=s[2] &&
   symbol-s[2]<=s[3] && 0xA8<=s[3]-(symbol-s[2]))return true;
 }
 return false;
}
inline bool textRange(const Layout &l,uint64_t address,uint64_t length) {
 if(l.q[1] || l.q[15] || !RPLSOutgoing::range(address,length))return false;
 for(unsigned i=0;i<l.q[9] && i<Segments;++i) {
  const auto s=l.segment[i];
  if((l.q[10]&(1ULL<<i)) && (s[7]&5)==5 && RPLSOutgoing::range(s[2],s[3]) &&
   address>=s[2] && address-s[2]<=s[3] && length<=s[3]-(address-s[2]))return true;
 }
 return false;
}
}
#endif
