//
//  SR-IOV VF contract used by the experimental WhateverXe backend.
//
//  This is deliberately a data-only boundary.  A VF cannot be sent through
//  the native PF MMIO/Display13 path: command submission and GGTT updates
//  must be relayed through the GuC/PF ABI instead.
//

#ifndef kern_xelp_sriov_vf_hpp
#define kern_xelp_sriov_vf_hpp

#include <stdint.h>

namespace XELPSRIOV {

static constexpr uint32_t A780Identity = 0xA7808086U;
static constexpr uint64_t PFBar2Length = 0x40000000ULL;
static constexpr uint64_t VFBar0Length = 0x01000000ULL;

// Register offsets from Intel's Gen12 i915 SR-IOV ABI.
static constexpr uint32_t VFCapabilityRegister = 0x1901F8U;
static constexpr uint32_t VFMasterIRQRegister = 0x190010U;
static constexpr uint32_t VFIRQStatus = 0x000U;
static constexpr uint32_t VFIRQSource = 0x400U;
static constexpr uint32_t VFIRQEnable = 0x440U;
static constexpr uint32_t VFRegisterStride = 0x1000U;

// VF2PF MMIO relay opcodes and fixed message sizes.
static constexpr uint16_t MMIOHandshakeOpcode = 0x01U;
static constexpr uint16_t MMIOUpdateGGTTOpcode = 0x02U;
static constexpr uint16_t MMIOGetRuntimeOpcode = 0x10U;
static constexpr uint16_t MMIOHandshakeWords = 4U;
static constexpr uint16_t MMIOUpdateGGTTWords = 4U;
static constexpr uint16_t MMIOGetRuntimeWords = 4U;
static constexpr uint32_t MMIOUpdateGGTTOffsetMask = 0xFFFFF000U;
static constexpr uint32_t MMIOUpdateGGTTModeMask = 0x00000C00U;
static constexpr uint32_t MMIOUpdateGGTTCopiesMask = 0x000003FFU;

struct Contract {
    uint32_t identity {0};
    uint16_t command {0};
    uint64_t bar0Length {0};
    uint64_t bar2Length {0};
    bool bar0Enabled {false};
    bool bar2Virtual {false};

    constexpr bool identityMatches() const { return identity == A780Identity; }
    constexpr bool aperturesMatch() const {
        return (bar0Length == 0 || bar0Length == VFBar0Length) &&
            bar2Length > 0 && bar2Length < PFBar2Length;
    }
    constexpr bool classified() const {
        return identityMatches() && aperturesMatch() && bar2Virtual;
    }
    constexpr bool memoryEnabled() const { return (command & 0x2U) != 0; }
    constexpr bool gucRelayRequired() const { return classified() && memoryEnabled(); }
};

} // namespace XELPSRIOV

#endif // kern_xelp_sriov_vf_hpp
