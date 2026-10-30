// Shared IOKit adapter. Call only after IOPCIDevice has been published.
#ifndef KERN_XELP_DEVICE_HPP
#define KERN_XELP_DEVICE_HPP
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>
#include "kern_xelp_platform.hpp"
namespace XELP {
inline Profile deviceProfile(IOPCIDevice *pci) {
    if(!pci)return {};
    // QEMU can intentionally mask a passed VF's Intel identity so Apple's
    // native Intel driver does not bind to a VF with no PF-style BAR0.  The
    // experimental VF profile still uses the logical A780 platform data while
    // the PCI function remains owned by the relay backend.
    auto identity=pci->configRead32(0);
    if(identity!=0xA7801AF4U && identity!=0xA7808086U &&
       ((identity&0xFFFFU)==0x1AF4U || (identity>>16)==0x1AF4U))
        identity=(static_cast<uint32_t>(pci->configRead16(2))<<16) | pci->configRead16(0);
    if(identity==0xA7801AF4U)
        return identify(0xA7808086U,pci->configRead8(8),0xA7808086U);
    const auto property=pci->getProperty("device-id");
    const auto data=OSDynamicCast(OSData,property);
    return select(identity,pci->configRead8(8),property!=nullptr,
        data ? data->getBytesNoCopy() : nullptr,data ? data->getLength() : 0);
}
}
#endif
