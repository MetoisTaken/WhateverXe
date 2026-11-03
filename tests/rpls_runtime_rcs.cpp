#include "../WhateverXe/kern_rpls_runtime_rcs.hpp"
#include <cassert>
#include <thread>
#include <vector>

int main() {
 using namespace RPLSRuntimeRCS;
 for(unsigned mask=0;mask<16;++mask) {
  State s;
  const bool accepted=s.claim(mask&1,mask&2,mask&4,mask&8,0);
  assert(accepted==(mask==15));
  assert(!s.submitted());
 }
 State engine;
 assert(!engine.claim(true,true,true,true,2));
 assert(engine.claim(true,true,true,true,0));
 assert(!engine.claim(true,true,true,true,0));
 assert(!engine.submitted() && !engine.pending());
 engine.prepared();assert(engine.pending() && !engine.submitted());
 engine.returned();assert(engine.submitted() && !engine.pending());
 assert(!engine.claim(true,true,true,true,0));
 for(unsigned client=0;client<=16;++client) {
  assert(!renderACK(UINT32_MAX,1,client));
  assert(!renderACK(0,1,client));
  assert(!renderACK(2,4,client));
  assert(!gtACK(UINT32_MAX,4,client));
  assert(!gtACK(0,4,client));
  assert(!gtACK(2,1,client));
  if(client<=15) {
   assert(renderACK(1U<<client,1,client));
   assert(renderACK(1U<<client,7,client));
   assert(gtACK(1U<<client,4,client));
   assert(gtACK(1U<<client,7,client));
   assert(!gtACK((1U<<client)^0xffff,4,client));
   assert(!renderACK((1U<<client)^0xffff,1,client));
  } else {assert(!renderACK(2,1,client));assert(!gtACK(2,4,client));}
 }
 // Only one competing submit can own the immutable descriptor metadata.
 State concurrent;unsigned winners=0;uint64_t identity=0;
 std::vector<std::thread> threads;
 for(unsigned i=0;i<8;++i)threads.emplace_back([&,i] {
  if(concurrent.claim(true,true,true,true,0)) {
   __atomic_fetch_add(&winners,1U,__ATOMIC_RELAXED);identity=0x100+i;
   concurrent.prepared();concurrent.returned();
  }
 });
 for(auto &thread:threads)thread.join();
 assert(winners==1 && concurrent.submitted() && identity>=0x100 && identity<0x108);
}
