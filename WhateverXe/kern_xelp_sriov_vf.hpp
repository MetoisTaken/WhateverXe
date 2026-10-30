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
static constexpr uint16_t MMIORelayAction = 0x5005U;
static constexpr uint32_t MMIORelayMagic = 0x0FU;
static constexpr uint16_t MMIOHandshakeWords = 4U;
static constexpr uint16_t MMIOUpdateGGTTWords = 4U;
static constexpr uint16_t MMIOGetRuntimeWords = 4U;
static constexpr uint32_t HXGOriginMask = 0x80000000U;
static constexpr uint32_t HXGTypeMask = 0x70000000U;
static constexpr uint32_t HXGRequestActionMask = 0x0000FFFFU;
static constexpr uint32_t HXGRequestData0Mask = 0x0FFF0000U;
static constexpr uint32_t HXGResponseData0Mask = 0x00FFFFFFU;
static constexpr uint32_t MMIORelayMagicMask = 0x0F000000U;
static constexpr uint32_t MMIORelayOpcodeMask = 0x00FF0000U;
static constexpr uint32_t MMIOUpdateGGTTOffsetMask = 0xFFFFF000U;
static constexpr uint32_t MMIOUpdateGGTTModeMask = 0x00000C00U;
static constexpr uint32_t MMIOUpdateGGTTCopiesMask = 0x000003FFU;

enum class GGTTMode : uint32_t {
    Duplicate = 0U,
    Replicate = 1U,
    DuplicateLast = 2U,
    ReplicateLast = 3U,
};

// This is the exact four-dword envelope consumed by intel_guc_send_mmio().
// The builders are pure: they only pack ABI words and never touch hardware.
struct MMIORequest {
    uint32_t words[4] {0, 0, 0, 0};
    uint32_t count {1};
};

constexpr uint32_t mmioRelayHeader(uint16_t opcode,
                                    uint32_t magic = MMIORelayMagic) {
    return ((magic & 0xFU) << 24) |
        ((static_cast<uint32_t>(opcode) & 0xFFU) << 16) |
        MMIORelayAction;
}

constexpr uint32_t packVersion(uint16_t major, uint16_t minor) {
    return (static_cast<uint32_t>(major) << 16) | minor;
}

constexpr uint32_t packGGTTControl(uint32_t offset,
                                    GGTTMode mode,
                                    uint16_t copies) {
    return (offset & MMIOUpdateGGTTOffsetMask) |
        ((static_cast<uint32_t>(mode) << 10) & MMIOUpdateGGTTModeMask) |
        (static_cast<uint32_t>(copies) & MMIOUpdateGGTTCopiesMask);
}

constexpr MMIORequest makeHandshakeRequest(uint16_t major, uint16_t minor) {
    return MMIORequest{{mmioRelayHeader(MMIOHandshakeOpcode),
                        packVersion(major, minor), 0U, 0U}, 4U};
}

constexpr MMIORequest makeGGTTRequest(uint32_t offset,
                                       GGTTMode mode,
                                       uint16_t copies,
                                       uint64_t pte) {
    return MMIORequest{{mmioRelayHeader(MMIOUpdateGGTTOpcode),
                        packGGTTControl(offset, mode, copies),
                        static_cast<uint32_t>(pte & 0xFFFFFFFFULL),
                        static_cast<uint32_t>(pte >> 32)}, 4U};
}

constexpr MMIORequest makeRuntimeRequest(uint32_t offset1,
                                          uint32_t offset2 = 0U,
                                          uint32_t offset3 = 0U) {
    return MMIORequest{{mmioRelayHeader(MMIOGetRuntimeOpcode),
                        offset1, offset2, offset3}, 4U};
}

constexpr bool requestHeaderValid(uint32_t header, uint16_t opcode) {
    return (header & (HXGOriginMask | HXGTypeMask)) == 0U &&
        (header & HXGRequestActionMask) == MMIORelayAction &&
        (header & MMIORelayOpcodeMask) ==
            ((static_cast<uint32_t>(opcode) & 0xFFU) << 16) &&
        (header & MMIORelayMagicMask) == (MMIORelayMagic << 24);
}

constexpr bool responseHeaderValid(uint32_t header, uint32_t magic = MMIORelayMagic) {
    return (header & HXGOriginMask) != 0U &&
        (header & HXGTypeMask) == 0x70000000U &&
        (header & MMIORelayMagicMask) == ((magic & 0xFU) << 24);
}

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
