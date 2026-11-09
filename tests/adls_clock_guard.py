#!/usr/bin/env python3
"""Exercise the runtime clock admission boundary against impostor objects."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
function=re.search(r'bool XE::adlsClockTarget\(.*?\n\}',s,re.S).group()
code='''#include "kern_alderlake_s.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct XE {
 bool adlsClockReady=true;void *tglObservationController;uint8_t tglPlatformCopy[0x1c0] {};
 AlderLakeS::Wiring adlsWiring;static bool adlsClockTarget(void*,void*,void*);
};
XE state;XE *callbackXE=&state;
FUNCTION
template<class T>T &at(void *p,unsigned off){return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+off);}
int main(){
 alignas(8) uint8_t ctl[0xF00] {},port[0x700] {},access[0x70] {};
 state.tglObservationController=ctl;state.adlsWiring.valid=true;state.adlsWiring.hdmi=8;state.adlsWiring.pin[3]=9;
 uint32_t config[]={3,9,2,0,0x800,0x202};auto c=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x68);
 memcpy(c,config,sizeof(config));at<void *>(port,0x548)=c;at<void *>(port,0x578)=access;
 at<void *>(ctl,0xC40)=access;at<uint32_t>(port,0x5B8)=0x6C10C;at<uint32_t>(access,0x60)=0x1000000;
 assert(XE::adlsClockTarget(ctl,port,nullptr));assert(XE::adlsClockTarget(ctl,port,port));
 assert(!XE::adlsClockTarget(nullptr,port,nullptr));assert(!XE::adlsClockTarget(ctl,nullptr,nullptr));
 assert(!XE::adlsClockTarget(ctl,port,access));assert(!XE::adlsClockTarget(access,port,nullptr));
 at<void *>(port,0x548)=config;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<void *>(port,0x548)=c;
 at<void *>(port,0x578)=nullptr;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<void *>(port,0x578)=access;
 at<uint32_t>(port,0x5B8)=0;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<uint32_t>(port,0x5B8)=0x6C10C;
 at<uint32_t>(access,0x60)=0x100000;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<uint32_t>(access,0x60)=0x1000000;
 for(unsigned i=0;i<6;++i){c[i]^=1;assert(!XE::adlsClockTarget(ctl,port,nullptr));c[i]^=1;}
 state.adlsWiring.hdmi|=1;assert(XE::adlsClockTarget(ctl,port,nullptr));state.adlsWiring.hdmi=8;
 state.adlsClockReady=false;assert(!XE::adlsClockTarget(ctl,port,nullptr));
 state.adlsClockReady=true;state.adlsWiring.valid=true;state.adlsWiring.dp=16;state.adlsWiring.pin[4]=10;
 uint32_t dp[]={4,10,1,0,0x400,0x201};c=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x80);
 memcpy(c,dp,sizeof(dp));at<void *>(port,0x548)=c;at<uint32_t>(port,0x5B8)=0x16010C;
 assert(XE::adlsClockTarget(ctl,port,nullptr));assert(XE::adlsClockTarget(ctl,port,port));
 state.adlsWiring.dp=0;assert(!XE::adlsClockTarget(ctl,port,nullptr));state.adlsWiring.valid=true;state.adlsWiring.dp=16;state.adlsWiring.pin[4]=10;
 at<void *>(port,0x548)=dp;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<void *>(port,0x548)=c;
 at<uint32_t>(port,0x5B8)=0x6C10C;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<uint32_t>(port,0x5B8)=0x16010C;
 at<uint32_t>(access,0x60)=0x164298;assert(!XE::adlsClockTarget(ctl,port,nullptr));at<uint32_t>(access,0x60)=0x1000000;
 for(unsigned i=0;i<6;++i){c[i]^=1;assert(!XE::adlsClockTarget(ctl,port,nullptr));c[i]^=1;}
 assert(!XE::adlsClockTarget(ctl,port,access));assert(!XE::adlsClockTarget(access,port,nullptr));
 for(unsigned ddi:{0u,3u,4u,5u,6u})for(bool isDP:{false,true}) {
  state.adlsWiring={};state.adlsWiring.valid=true;
  (isDP?state.adlsWiring.dp:state.adlsWiring.hdmi)=1U<<ddi;
  state.adlsWiring.pin[ddi]=isDP?(ddi?ddi+6:1):12;
  AlderLakeS::Port route;assert(AlderLakeS::Port::resolve(ddi,route));
  uint32_t config[]={ddi,state.adlsWiring.pin[ddi],isDP?1U:2U,0,isDP?0x400U:0x800U,isDP?0x201U:0x202U};
  auto bound=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x20+24*ddi);memcpy(bound,config,sizeof(config));
  at<void *>(port,0x548)=bound;at<uint32_t>(port,0x5B8)=route.phyBase()+0x10C;
  assert(XE::adlsClockTarget(ctl,port,nullptr)==(ddi<=5));
  state.adlsWiring.valid=false;assert(!XE::adlsClockTarget(ctl,port,nullptr));
 }
 state={};state.adlsClockReady=true;state.tglObservationController=ctl;state.adlsWiring.valid=true;state.adlsWiring.panel=1;
 const uint32_t panel[]={0,0,0,0,2,0x18};auto panelConfig=reinterpret_cast<uint32_t*>(state.tglPlatformCopy+0x20);memcpy(panelConfig,panel,sizeof(panel));
 at<void*>(port,0x548)=panelConfig;at<void*>(port,0x578)=access;at<void*>(ctl,0xc40)=access;
 at<uint32_t>(port,0x5b8)=0x16210c;at<uint32_t>(access,0x60)=0x200000;
 assert(XE::adlsClockTarget(ctl,port,nullptr));
 state.adlsWiring.panel=0;assert(!XE::adlsClockTarget(ctl,port,nullptr));
 puts("PASS ADL-S runtime clock guard: exact owner, immutable TC1/TC2 configurations, mapped Combo B/C and no shared port");
}'''.replace('FUNCTION',function)
with tempfile.TemporaryDirectory(prefix='adls-clock-guard-') as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
