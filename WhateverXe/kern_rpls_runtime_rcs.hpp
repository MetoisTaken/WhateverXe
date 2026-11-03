// One immutable runtime observation. This gate never schedules GPU work.
#ifndef KERN_RPLS_RUNTIME_RCS_HPP
#define KERN_RPLS_RUNTIME_RCS_HPP
#include <stdint.h>
namespace RPLSRuntimeRCS {
struct State {
 uint32_t phase {}; // 0 unselected, 1 claiming, 2 native submit pending, 3 returned
 bool claim(bool admitted,bool ready,bool runtime,bool owned,unsigned engine) {
  if(!admitted || !ready || !runtime || !owned || engine)return false;
  uint32_t empty=0;
  return __atomic_compare_exchange_n(&phase,&empty,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
 }
 void prepared() {__atomic_store_n(&phase,2U,__ATOMIC_RELEASE);}
 void returned() {__atomic_store_n(&phase,3U,__ATOMIC_RELEASE);}
 bool pending() const {return __atomic_load_n(&phase,__ATOMIC_ACQUIRE)==2;}
 bool submitted() const {return __atomic_load_n(&phase,__ATOMIC_ACQUIRE)==3;}
};
inline bool renderACK(uint32_t ack,unsigned domains,unsigned client) {
 return (domains&1) && client<=15 && ack!=UINT32_MAX && (ack&(1U<<client));
}
inline bool gtACK(uint32_t ack,unsigned domains,unsigned client) {
 return (domains&4) && client<=15 && ack!=UINT32_MAX && (ack&(1U<<client));
}
}
#endif
