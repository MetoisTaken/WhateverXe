#include <cassert>
#include <cstring>
#include <vector>
#include <string>
#include "kern_rpls_memory.hpp"
using mach_vm_address_t=uintptr_t;
struct KernelPatcher {
 struct KextInfo{};struct LookupPatch{KextInfo *info;const uint8_t *find,*replace;size_t size,count;};
 enum class Error{NoError,Failure};
 uintptr_t base=0;unsigned solves=0,patches=0;int wrong=-1,fail=-1,corrupt=-1;Error error=Error::NoError;
 template<class T>T solveSymbol(size_t,const char *n,uintptr_t,size_t){
  const char *names[]={"__ZN25IGHardwareGlobalPageTable8mapRangeERK14IGAddressRangeyy","__ZN25IGHardwareGlobalPageTable15mapRangeRotatedER33IGAddressRangeRotatedPageIteratorR25IGPhysicalSegmentIteratory","__ZN25IGHardwareGlobalPageTable13mapRangeDummyERK14IGAddressRangey"};
  ++solves;for(unsigned i=0;i<3;++i)if(!strcmp(n,names[i]))return T(base+RPLSMemory::Functions[i]+(int(i)==wrong?1:0));return 0;
 }
 void applyLookupPatch(LookupPatch *p,uint8_t *site,size_t n){
  unsigned i=patches++;assert(p->info&&p->size==13&&p->count==1&&n==14&&!memcmp(site,p->find,13));
  if(int(i)==fail){error=Error::Failure;return;}memcpy(site,p->replace,13);if(int(i)==corrupt)site[0]^=1;
 }
 Error getError(){return error;}void clearError(){error=Error::NoError;}
};
struct OSData {
 std::vector<uint8_t> bytes;
 static OSData *withBytes(const void *p,size_t n){auto d=new OSData;auto b=static_cast<const uint8_t *>(p);d->bytes.assign(b,b+n);return d;}
 void release(){delete this;}
};
struct IORegistryEntry {
 std::vector<uint8_t> data;unsigned publishes=0;
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}
 void setProperty(const char *key,OSData *d){assert(!strcmp(key,"WEXRPLSMemory"));data=d->bytes;++publishes;}
};
#include "kern_rpls_memory.inc.hpp"
void run(bool requested,bool admitted,int badSymbol=-1,int fail=-1,int corrupt=-1,size_t size=0x10800){
 std::vector<uint8_t> image(0x10800,0xAA);for(unsigned i=0;i<3;++i)memcpy(image.data()+RPLSMemory::Sites[i],RPLSMemory::Instructions[i],13);
 auto original=image;KernelPatcher kp;KernelPatcher::KextInfo info;kp.base=reinterpret_cast<uintptr_t>(image.data());kp.wrong=badSymbol;kp.fail=fail;kp.corrupt=corrupt;
 rplsMemoryRequested=requested;rplsMemoryAdmitted=admitted;auto root=IORegistryEntry::getRegistryRoot();root->data.clear();root->publishes=0;
 rplsMemoryInstall(kp,3,kp.base,size,&info);
 const bool good=requested&&admitted&&badSymbol<0&&fail<0&&corrupt<0&&size==image.size();assert(RPLSMemory::ready(rplsMemoryReceipt)==good);
 if(!admitted){assert(kp.solves==0&&kp.patches==0&&root->publishes==0);}
 else{assert(root->publishes==1&&root->data.size()==128&&!memcmp(root->data.data(),&rplsMemoryReceipt,128));}
 if(!requested||!admitted||badSymbol>=0||size<0x1034A){assert(kp.patches==0&&image==original);}
 if(good){assert(kp.patches==3);for(unsigned i=0;i<image.size();++i){bool changed=false;for(auto site:RPLSMemory::Sites)changed|=i==site+2||i==site+3;if(!changed)assert(image[i]==original[i]);}}
}
int main(){run(false,false);run(true,false);run(false,true);run(true,true);run(true,true,-1,-1,-1,32);
 for(int i=0;i<3;++i){run(true,true,i);run(true,true,-1,i);run(true,true,-1,-1,i);}
}
