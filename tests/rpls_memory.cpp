#include <cassert>
#include <cstring>
#include "kern_rpls_memory.hpp"
using namespace RPLSMemory;
struct IO {
 uint8_t bytes[3][13];unsigned calls=0,writes=0;
 int badSymbol=-1,badCopy=-1,failPatch=-1,badReadback=-1;
 IO(){memcpy(bytes,Instructions,sizeof(bytes));}
 unsigned index(unsigned site){for(unsigned i=0;i<3;++i)if(site==Sites[i])return i;assert(false);return 0;}
 uintptr_t symbol(unsigned i){++calls;return Functions[i]+(int(i)==badSymbol?1:0);}
 bool copy(unsigned site,uint8_t *out,size_t n){++calls;auto i=index(site);if(int(i)==badCopy)return false;assert(n==13);memcpy(out,bytes[i],n);return true;}
 bool patch(unsigned site,const uint8_t *before,const uint8_t *after,size_t n){++calls;auto i=index(site);++writes;assert(n==13 && !memcmp(before,bytes[i],n));if(int(i)==failPatch)return false;memcpy(bytes[i],after,n);if(int(i)==badReadback)bytes[i][0]^=1;return true;}
};
int main(){
 assert(admitted(0xA7808086,4,true));assert(!admitted(0xA7808086,3,true));assert(!admitted(0x46808086,4,true));assert(!admitted(0xA7808086,4,false));
 {IO io;auto r=install(io,true,false);assert(!ready(r)&&!io.calls&&!io.writes);}
 {IO io;auto r=install(io,false,true);assert(r.q[3]==1 && !r.q[4]&&!ready(r)&&!io.writes);for(unsigned i=0;i<3;++i)assert(r.q[6+i]==Old&&r.q[9+i]==Old);}
 {IO io;auto r=install(io,true,true);assert(ready(r)&&io.writes==3);for(unsigned i=0;i<3;++i)assert(mask(io.bytes[i])==New);
  for(unsigned i=0;i<13;++i){auto altered=r;altered.q[i]^=1;assert(!ready(altered));}}
 for(unsigned site=0;site<3;++site){
  for(unsigned byte=0;byte<13;++byte)for(unsigned bit=0;bit<8;++bit){IO io;io.bytes[site][byte]^=1U<<bit;auto r=install(io,true,true);assert(!ready(r)&&!io.writes);}
  {IO io;io.badSymbol=site;auto r=install(io,true,true);assert(!ready(r)&&!io.writes);}
  {IO io;io.badCopy=site;auto r=install(io,true,true);assert(!ready(r)&&!io.writes);}
  {IO io;io.failPatch=site;auto r=install(io,true,true);assert(!ready(r)&&io.writes==site+1);}
  {IO io;io.badReadback=site;auto r=install(io,true,true);assert(!ready(r)&&io.writes==site+1);}
  uint8_t out[13]{};assert(prepare(site,Instructions[site],out,13));assert(mask(out)==New);
  for(unsigned j=0;j<13;++j)if(j!=2&&j!=3)assert(out[j]==Instructions[site][j]);
 }
 uint64_t state=0xA78004;
 for(unsigned i=0;i<10000;++i){state=state*6364136223846793005ULL+1442695040888963407ULL;const auto pa=state&0x7FFFFFF000ULL,flags=state>>17;const auto old=pa|(flags&Old)|1,newPte=pa|(flags&New)|1;
  assert((old^newPte)==(old&0xFFEULL));assert((old&0x7FFFFFF000ULL)==(newPte&0x7FFFFFF000ULL));assert((newPte&4095)==1);}
}
