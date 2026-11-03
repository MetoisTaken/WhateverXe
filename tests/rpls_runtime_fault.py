#!/usr/bin/env python3
"""Execute production GT observation and wake selection against guarded MMIO."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
begin = source.index('struct TGLFaultRecord ')
fault = source[begin:source.index('static bool tglExecutionSerialize(', begin)]
wake_start = source.index('void XE::wrapAlderLakeNExecutionWake(')
wake = source[source.index(' if(rplsMemoryAdmitted) {', wake_start):
              source.index(' if(!callbackXE->adlnDetected', wake_start)]
assert source.index('FunctionCast(wrapAlderLakeNExecutionWake,', wake_start) < source.index(wake, wake_start)

prefix = r'''
#include "kern_rpls_runtime_rcs.hpp"
#include <cassert>
#include <cstring>
#include <cstdint>
#include <vector>
#include <thread>
#include <sys/mman.h>
#include <unistd.h>
template<class T,size_t N>constexpr size_t arrsize(const T(&)[N]){return N;}
struct OSSerialize {std::vector<uint8_t> bytes;};
struct OSData {
 std::vector<uint8_t> bytes;
 static OSData *withCapacity(size_t n){auto p=new OSData;p->bytes.reserve(n);return p;}
 bool appendBytes(const void *p,size_t n){auto b=static_cast<const uint8_t *>(p);bytes.insert(bytes.end(),b,b+n);return true;}
 bool serialize(OSSerialize *s){s->bytes=bytes;return true;}void release(){delete this;}
};
static uint8_t ownerBytes[0x1400],otherOwner[0x1400];
static struct {void *owner=ownerBytes;} tglHost;
static volatile uint8_t *bar;
static volatile uint8_t *tglHostBase(){return bar;}
static uint64_t ticks=100;
static void tglHostClock(uint64_t *q){*q=__atomic_add_fetch(&ticks,1ULL,__ATOMIC_RELAXED);}
static uintptr_t taskThread=0x1234;
static void *current_thread(){return reinterpret_cast<void *>(taskThread);}
static bool rplsMemoryAdmitted=true,rplsRuntimeReady=true;
static RPLSRuntimeRCS::State rplsRuntimeState;
static uint64_t rplsRuntimeThread=0x1234,tglExecutionStart=1,tglExecutionStreamer=2,tglExecutionDescriptor=3;
static uint32_t rplsRuntimeNativeDepth=1,tglExecutionSubmit=4;
static struct {uint32_t ready;} tglExecution[10];
static unsigned renderCalls;
static void tglExecutionSample(unsigned slot,uint8_t *,unsigned,unsigned,uint64_t,uint32_t,uint64_t,bool,bool){
 ++renderCalls;tglExecution[slot].ready=1;
}
'''
main = r'''
int main(){
 const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE)),size=0x140000;
 bar=static_cast<uint8_t *>(mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(bar!=MAP_FAILED);
 const size_t ackPage=0x130044&~(page-1);
 assert(!mprotect(const_cast<uint8_t *>(bar)+ackPage,page,PROT_READ|PROT_WRITE));
 auto ack=reinterpret_cast<volatile uint32_t *>(bar+0x130044);
 // Every fault/AUX/PAT page stays inaccessible until all admission cases run.
 *ack=UINT32_MAX;tglFaultSample(0,ownerBytes,1,true);assert(!tglFault[0].ready);
 *ack=0;tglFaultSample(0,ownerBytes,1,true);assert(!tglFault[0].ready);
 *ack=1;tglFaultSample(0,ownerBytes,1,true);assert(!tglFault[0].ready);
 *ack=2;tglFaultSample(0,ownerBytes,16,true);assert(!tglFault[0].ready);
 tglFaultSample(0,otherOwner,1,true);assert(!tglFault[0].ready);
 ownerBytes[0x1191]=0x10;tglFaultSample(0,ownerBytes,1,true);assert(!tglFault[0].ready);
 ownerBytes[0x1191]=0;
 // Production wake selection must reject startup and unowned pending frames.
 rplsRuntimeState.phase=0;wake(ownerBytes,true,4,1);
 rplsRuntimeState.phase=1;wake(ownerBytes,true,4,1);
 rplsRuntimeState.prepared();taskThread=0x9999;wake(ownerBytes,true,4,1);
 taskThread=0x1234;rplsRuntimeNativeDepth=2;wake(ownerBytes,true,4,1);
 rplsRuntimeNativeDepth=1;wake(ownerBytes,false,4,1);wake(otherOwner,true,4,1);
 wake(ownerBytes,true,2,1);wake(ownerBytes,true,1,1);wake(ownerBytes,true,4,16);
 rplsRuntimeReady=false;wake(ownerBytes,true,4,1);rplsRuntimeReady=true;
 rplsMemoryAdmitted=false;wake(ownerBytes,true,4,1);rplsMemoryAdmitted=true;
 assert(!tglFault[0].ready && !renderCalls);
 assert(!mprotect(const_cast<uint8_t *>(bar),size,PROT_READ|PROT_WRITE));
 auto reg=[](unsigned r)->volatile uint32_t &{return *reinterpret_cast<volatile uint32_t *>(bar+r);};
 reg(0xcec4)=0x123;reg(0xceb8)=0x40328;reg(0xcebc)=0x10;
 for(unsigned i=0;i<8;++i)reg(0x4800+4*i)=i+10;
 reg(0x4200)=0x40008000;reg(0x4204)=0;reg(0x4208)=0;
 std::vector<uint8_t> before(size);memcpy(before.data(),const_cast<uint8_t *>(bar),size);
 // GT-only wake now reaches the selected pending interval, without render sampling.
 wake(ownerBytes,true,4,1);
 assert(tglFault[0].ready==1 && !renderCalls);
 auto q=tglFault[0].q;
 assert(q[0]==4 && q[4]==2 && q[6]==0x123 && q[7]==0x40328 && q[8]==0x10 && q[9]==0x123);
 assert(q[22]==2 && q[23]==1 && q[21]==17 && q[11]==0x40008000);
 assert(!memcmp(before.data(),const_cast<uint8_t *>(bar),size));
 const auto firstTime=q[1];wake(ownerBytes,true,4,1);assert(q[1]==firstTime);
 // Post-return samples are bounded and one per qualifying native GT wake.
 rplsRuntimeState.returned();ticks=6000000000ULL;
 for(unsigned i=1;i<5;++i){
  wake(ownerBytes,true,4,1);assert(tglFault[i].ready==1);
  if(i<4)assert(!tglFault[i+1].ready);
 }
 assert(!renderCalls);wake(ownerBytes,true,7,1);assert(renderCalls==1);
 for(const auto &r:tglFault)assert(r.ready==1);
 // In-progress records serialize as absent; legacy header remains schema3.
 tglFault[2].ready=2;memset(tglFault[2].q,0xab,sizeof(tglFault[2].q));
 OSSerialize out;assert(tglFaultSerialize(nullptr,reinterpret_cast<void *>(1),&out));
 assert(out.bytes.size()==1032);auto data=reinterpret_cast<const uint64_t *>(out.bytes.data());
 assert(data[0]==4 && data[1]==5 && data[2]==24 && data[3]==14);
 for(unsigned i=0;i<25;++i)assert(!data[4+2*25+i]);
 assert(tglFaultSerialize(nullptr,nullptr,&out));assert(reinterpret_cast<const uint64_t *>(out.bytes.data())[0]==3);
 // A single writer wins immutable publication under competing native wakes.
 tglFault[2]={};*ack=0xffff;
 std::vector<std::thread> threads;
 for(unsigned i=0;i<8;++i)threads.emplace_back([i]{tglFaultSample(2,ownerBytes,i,true);});
 for(auto &thread:threads)thread.join();
 assert(tglFault[2].ready==1 && tglFault[2].q[3]<8 && tglFault[2].q[22]==0xffff && tglFault[2].q[23]);
 munmap(const_cast<uint8_t *>(bar),size);
}
'''
with tempfile.TemporaryDirectory(prefix='rpls-runtime-fault-') as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(prefix + fault +
        'static void wake(void *owner,bool enable,unsigned domains,unsigned client) {\n' + wake + '}\n' + main)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I' + str(root / 'WhateverXe'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS runtime GT fault: inaccessible-MMIO guards, pending ownership, GT-only wakes, bounded immutable publication')
