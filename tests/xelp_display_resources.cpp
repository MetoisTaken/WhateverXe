#include "kern_alderlake_s.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELP;
static_assert(ADLSComboResources::physicalMask()==0x79,"physical ADL-S ports");
static_assert(ADLSComboBackend::mask(Donor::C004FC1A)==0x39,"Combo backend capacity");
int main() {
 const uint32_t physical[]={0,3,4,5,6};
 for(unsigned n=0;n<5;++n) {
  assert(ADLSComboResources::ddi(n)==physical[n]);assert(ADLSComboResources::phy(physical[n])==n);
  AlderLakeS::Port p;assert(AlderLakeS::Port::resolve(physical[n],p) && p.phy==n);
 }
 assert(ADLSComboResources::ddi(5)==9 && ADLSComboResources::ddi(~0U)==9);
 for(unsigned d=0;d<32;++d)for(auto donor:{Donor::Unknown,Donor::C004FC1A,Donor(2)}) {
  uint32_t slot=0;const bool known=donor==Donor::C004FC1A&&(d==0||d==3||d==4||d==5);
  assert(ADLSComboBackend::clockSlot(donor,d,slot)==known);
  // The native index replacement is d>>1 within the selected capacity. DDI 5 is
  // PHY slot 3, driven through the donor's DDI 4 path.
  assert(slot==(known?(d==5?3:d>>1):~0U));
 }
 // All masks over the nine donor port records, independent of VBT protocol.
 // Physical E remains recognizable but never becomes a usable slot.
 for(unsigned mask=0;mask<512;++mask) {
  ProbeConfig c;c.profile=identify(0x46908086,12);c.combo=mask;
  c.comboBackend=ADLSComboBackend::mask(c.profile.donor);
  assert(c.valid()==(mask && !(mask&~0x39U)));
  c.comboBackend=ADLSComboResources::physicalMask();assert(!c.valid());
 }
 assert(!ADLSComboBackend::mask(Donor::Unknown));
 puts("PASS physical PHY mapping separate from donor slots: 96 slot cases and 512 provider masks, no E resource inference");
}
