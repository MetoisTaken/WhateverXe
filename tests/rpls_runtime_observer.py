#!/usr/bin/env python3
"""Execute production observation/serialization with guarded MMIO and mock CFData."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
sample_start = source.index('static const uint32_t tglExecutionRegs[]')
sample = source[sample_start:source.index('static void adlnCaptureKernelWaitReturn', sample_start)]
serializer = source[source.index('static bool tglExecutionSerialize('):
                    source.index('// Latest retained native successful map request')]
# A nested same-thread submit before the original call must already see depth1.
selected = source[source.index(' if(rplsFirst) {'):source.index(' if(tglSameTailRestoreEnabled', source.index(' if(rplsFirst) {'))]
assert selected.index('rplsRuntimeNativeDepth=1;') < selected.index('rplsRuntimeState.prepared();')
assert 'if(rplsNative && !rplsFirst)++rplsRuntimeNativeDepth;' in source
prepare = selected[selected.index('  rplsRuntimeNativeDepth=1;'):selected.index('  tglExecutionSample(2,')]
native_begin = source.index(' const bool rplsNative=')
native_call = source.index(' FunctionCast(wrapTGLHostSubmit,callbackXE->orgTGLHostSubmit)(streamer,flags);', native_begin)
native_end = source.index(' if(renderWakeOwner)tglSubmitHistoryRecord(renderWakeOwner,streamer,n,1);', native_call)
native_before = source[native_begin:native_call]
native_after = source[source.index('\n', native_call)+1:native_end]
load = source[source.index('void tglHostLoadSnapshot('):source.index('// r125:')]
assert load.index('rplsRuntimeCaptureBeforeLoad') < load.index('n>=arrsize(tglHost.load)')

prefix = r'''
#include "kern_rpls_runtime_rcs.hpp"
#include <cassert>
#include <cstring>
#include <cstdint>
#include <vector>
#include <thread>
#include <functional>
#include <sys/mman.h>
#include <unistd.h>
template<class T,size_t N>constexpr size_t arrsize(const T(&)[N]){return N;}
struct OSSerialize {std::vector<uint8_t> bytes;};
struct OSData {
 std::vector<uint8_t> bytes;
 static OSData *withCapacity(size_t n){auto p=new OSData;p->bytes.reserve(n);return p;}
 static OSData *withBytes(const void *p,size_t n){auto d=withCapacity(n);d->appendBytes(p,n);return d;}
 bool appendBytes(const void *p,size_t n){auto b=static_cast<const uint8_t *>(p);bytes.insert(bytes.end(),b,b+n);return true;}
 bool serialize(OSSerialize *s){s->bytes=bytes;return true;}void release(){delete this;}
};
static uint8_t ownerBytes[0x1400];
static struct {void *owner=ownerBytes;} tglHost;
static volatile uint8_t *bar;
static volatile uint8_t *tglHostBase(){return bar;}
static uint64_t ticks;
static void tglHostClock(uint64_t *q){*q=__atomic_add_fetch(&ticks,1ULL,__ATOMIC_RELAXED);}
static void *current_thread(){return reinterpret_cast<void *>(0x1234);}
static bool checkKernelArgument(const char *){return false;}
static bool adlnKernelContextEntered;
'''
nesting = 'static void prepareSelected() {\n' + prepare + '}\n'
nesting += 'static void nativeSelected(bool rplsFirst, void *streamer, const std::function<void()> &native) {\n' + native_before + ' native();\n' + native_after + '}\n'

main = r'''
int main(){
 const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE)),size=0x20000;
 bar=static_cast<uint8_t *>(mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(bar!=MAP_FAILED);assert(!mprotect(const_cast<uint8_t *>(bar),page,PROT_READ|PROT_WRITE));
 auto ack=reinterpret_cast<volatile uint32_t *>(bar+0xd84);
 // All register pages remain inaccessible. A forbidden read would fault.
 *ack=UINT32_MAX;tglExecutionSample(2,ownerBytes,1,1,3,4,5,false,true);
 assert(!tglExecution[2].ready);
 *ack=0;tglExecutionSample(2,ownerBytes,1,1,3,4,5,false,true);
 assert(!tglExecution[2].ready);
 *ack=2;tglExecutionSample(2,ownerBytes,4,1,3,4,5,false,true);
 assert(!tglExecution[2].ready);
 ownerBytes[0x1191]=0x10;tglExecutionSample(2,ownerBytes,1,1,3,4,5,false,true);
 assert(!tglExecution[2].ready);ownerBytes[0x1191]=0;
 tglExecutionSample(2,ownerBytes,1,16,3,4,5,false,true);assert(!tglExecution[2].ready);
 assert(!mprotect(const_cast<uint8_t *>(bar),size,PROT_READ|PROT_WRITE));
 for(unsigned i=0;i<arrsize(tglExecutionRegs);++i)
  *reinterpret_cast<volatile uint32_t *>(bar+tglExecutionRegs[i])=tglExecutionRegs[i];
 *ack=2;tglExecutionSample(2,ownerBytes,1,1,3,4,5,false,true);
 assert(tglExecution[2].ready==1 && tglExecution[2].q[0]==7 && tglExecution[2].q[14]==2 && tglExecution[2].q[15]);
 const auto retained=tglExecution[2].q[1];
 tglExecutionSample(2,ownerBytes,1,1,33,44,55,false,true);
 assert(tglExecution[2].q[1]==retained && tglExecution[2].q[3]==3);
 // An in-progress record must serialize as absent, never a partial copy.
 tglExecution[3].ready=2;memset(tglExecution[3].q,0xab,sizeof(tglExecution[3].q));
 OSSerialize out;assert(tglExecutionSerialize(nullptr,reinterpret_cast<void *>(1),&out));
 assert(out.bytes.size()==5232);auto q=reinterpret_cast<const uint64_t *>(out.bytes.data());
 assert(q[0]==7 && q[1]==10 && q[2]==64 && q[3]==45);
 assert(q[4+2*65]==1 && q[5+2*65]==7);
 for(unsigned i=0;i<65;++i)assert(q[4+3*65+i]==0);
 assert(tglExecutionSerialize(nullptr,nullptr,&out));
 assert(reinterpret_cast<const uint64_t *>(out.bytes.data())[0]==6);
 std::vector<std::thread> threads;
 for(unsigned i=0;i<8;++i)threads.emplace_back([i] {tglExecutionSample(4,ownerBytes,1,1,100+i,4,5,false,true);});
 for(auto &thread:threads)thread.join();
 assert(tglExecution[4].ready==1 && tglExecution[4].q[3]>=100 && tglExecution[4].q[3]<108);
 // Execute the exact production depth/publication bracket with nested calls.
 rplsRuntimeReady=true;rplsRuntimeThread=0x1234;tglExecutionStreamer=0x100;
 rplsRuntimeState.phase=1;prepareSelected();
 auto selectedStream=reinterpret_cast<void *>(0x100);
 assert(rplsRuntimeNativeDepth==1 && rplsRuntimeState.pending());
 // Interrupt reentry between prepared() and the outer original call.
 nativeSelected(false,selectedStream,[]{assert(rplsRuntimeNativeDepth==2 && rplsRuntimeState.pending());});
 assert(rplsRuntimeNativeDepth==1 && rplsRuntimeState.pending());
 nativeSelected(true,selectedStream,[&]{
  assert(rplsRuntimeNativeDepth==1 && rplsRuntimeState.pending());
  nativeSelected(false,selectedStream,[]{assert(rplsRuntimeNativeDepth==2 && rplsRuntimeState.pending());});
  assert(rplsRuntimeNativeDepth==1 && rplsRuntimeState.pending());
  nativeSelected(false,reinterpret_cast<void *>(0x200),[]{assert(rplsRuntimeNativeDepth==1);});
 });
 assert(!rplsRuntimeNativeDepth && rplsRuntimeState.submitted());
 // A later submit cannot reopen the retained native observation window.
 nativeSelected(false,selectedStream,[]{assert(!rplsRuntimeNativeDepth && rplsRuntimeState.submitted());});
 // Selection and pre-SQ metadata have separate publication phases.
 rplsRuntimeState.phase=1;rplsRuntimeThread=999;rplsRuntimeCaptureState=1;
 memset(rplsRuntimeCaptureMeta,0xcd,sizeof(rplsRuntimeCaptureMeta));
 assert(rplsRuntimeSerialize(nullptr,nullptr,&out));q=reinterpret_cast<const uint64_t *>(out.bytes.data());
 assert(q[2]==1 && q[3]==1);for(unsigned i=4;i<16;++i)assert(!q[i]);
 rplsRuntimeState.prepared();assert(rplsRuntimeSerialize(nullptr,nullptr,&out));
 q=reinterpret_cast<const uint64_t *>(out.bytes.data());assert(q[4]==999);for(unsigned i=9;i<16;++i)assert(!q[i]);
 rplsRuntimeCaptureState=2;assert(rplsRuntimeSerialize(nullptr,nullptr,&out));
 q=reinterpret_cast<const uint64_t *>(out.bytes.data());assert(q[9]==0xcdcdcdcdcdcdcdcdULL && !q[15]);
 munmap(const_cast<uint8_t *>(bar),size);
}
'''
with tempfile.TemporaryDirectory(prefix='rpls-runtime-observer-') as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(prefix + sample + serializer + nesting + main)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I' + str(root / 'WhateverXe'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS runtime observer: inaccessible-MMIO ACK guards, single-writer records, nested native bracket, immutable/partial serializers')
