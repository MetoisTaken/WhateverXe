// Copyright © 2018 vit9696. Derived from WhateverGreen (BSD-3-Clause).
#include "kern_host.hpp"
#include "kern_xelp_device.hpp"
#include "kern_xe_data_access.hpp"
#include "kern_backlight_profile.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_iokit.hpp>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IORegistryEntry.h>
XeHost *XeHost::callbackHost;
struct FramebufferViewer : public IOFramebuffer {
 static IOMemoryMap *&getVramMap(IOFramebuffer *fb) { return static_cast<FramebufferViewer *>(fb)->fVramMap; }
};
static const char *graphicsPath[] { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };
static KernelPatcher::KextInfo graphics { "com.apple.iokit.IOGraphicsFamily",graphicsPath,arrsize(graphicsPath),{true},{},KernelPatcher::KextInfo::Unloaded };
static const char *backlightPath[] { "/System/Library/Extensions/AppleBacklight.kext/Contents/MacOS/AppleBacklight" };
static KernelPatcher::KextInfo backlight { "com.apple.driver.AppleBacklight",backlightPath,arrsize(backlightPath),{true},{},KernelPatcher::KextInfo::Unloaded };
static IOPCIDevice *xelpPublishedVF;
static uint32_t xelpPCIIdentity(IOPCIDevice *pci) {
 if(!pci)return 0;
 const auto raw=pci->configRead32(0);
 const auto words=(static_cast<uint32_t>(pci->configRead16(2))<<16) | pci->configRead16(0);
 // Keep the raw values on the service while the opt-in VF diagnostic is
 // active; this also covers IOPCIDevice names such as S80@10 whose IOName is
 // published separately from getName().
 pci->setProperty("WhateverXeRawConfig32",raw,32);
 pci->setProperty("WhateverXeRawConfigWords",words,32);
 if(words==0xA7808086U || words==0xA7801AF4U)return words;
 if(raw==0xA7808086U || raw==0xA7801AF4U)return raw;
 if((raw&0xFFFFU)!=0x1AF4U && (raw>>16)!=0x1AF4U)return raw;
 // IOPCIDevice exposes the two configuration words independently on macOS;
 // use them when a virtual PCI function presents the dword in the opposite
 // byte order used by QEMU's x-pci identity override.
 return (static_cast<uint32_t>(pci->configRead16(2))<<16) | pci->configRead16(0);
}
// DeviceInfo normally exposes only the primary IGPU. A passed-through VF can
// remain a secondary PCI service beside QEMU's primary display, so the opt-in
// diagnostic path must locate the VF before the host dispatches XE.
static IOPCIDevice *xelpFindSRIOVVFCandidate(bool requireAperture) {
 // Observe mode records a passed VF without selecting it as Apple's primary
 // display. This gives the future VF backend a bootable control experiment.
 const bool requested=checkKernelArgument("-wex-sriov-vf") || checkKernelArgument("-wex-sriov-vf-observe");
 IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeSRIOVVFScanRequested",requested);
 if(!requested)return nullptr;
 if(xelpPublishedVF) {
  auto bar2=xelpPublishedVF->getDeviceMemoryWithRegister(0x18);
  const auto bar2Length=bar2 ? bar2->getLength() : 0;
  if(!requireAperture || (bar2Length>0 && bar2Length<0x40000000)) {
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeSRIOVVFMatchCount",1U,32);
   xelpPublishedVF->setProperty("WhateverXeSRIOVVFCandidate",true);
   return xelpPublishedVF;
  }
 }
 auto iterator=IORegistryIterator::iterateOver(gIOServicePlane,kIORegistryIterateRecursively);
 if(!iterator)return nullptr;
 IOPCIDevice *found=nullptr;
 uint32_t scanned=0,registryPCI=0,matches=0;
 while(auto entry=iterator->getNextObject()) {
  ++scanned;
  const auto serviceName=entry->getName();
  if(serviceName && serviceName[0]=='S')++registryPCI;
  char servicePath[256] {};
  int servicePathLength=sizeof(servicePath);
  const bool hasPath=entry->getPath(servicePath,&servicePathLength,gIOServicePlane)==kIOReturnSuccess;
  // Published PCI nodes carry pcidebug/vendor-id/device-id properties even
  // while their metaclass is not visible to a third-party kext's OSDynamicCast.
  // Use that stable registry contract to obtain the IOPCIDevice service.
  auto pci=(serviceName && serviceName[0]=='S') ? static_cast<IOPCIDevice *>(entry) : nullptr;
  if(!pci)continue;
  const auto identity=xelpPCIIdentity(pci);
  // 1af4:a780 is the QEMU generic identity used to keep Apple's native
  // Intel matcher away from the VF.  WhateverXe maps it back to the logical
  // A780 profile only on this explicit diagnostic path.
  if(serviceName && serviceName[0]=='S')
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeLastPCIService",serviceName);
  const bool serviceVFName=serviceName && (strcmp(serviceName,"S80")==0 || strstr(serviceName,"S80@")!=nullptr);
  if(serviceVFName)
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeVFServiceSeen",serviceName);
  if(hasPath && strstr(servicePath,"S80"))
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeLastPCIPath",servicePath);
  const bool qemuVFName=serviceVFName || (hasPath && strstr(servicePath,"S80")!=nullptr);
  if(qemuVFName)IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeVFNameSeen",true);
  if(identity!=0xA7808086U && identity!=0xA7801AF4U && !qemuVFName)continue;
  ++matches;
  pci->setProperty("WhateverXeSRIOVVFCandidate",true);
  auto bar2=pci->getDeviceMemoryWithRegister(0x18);
  const auto bar2Length=bar2 ? bar2->getLength() : 0;
  if(!requireAperture || (bar2Length>0 && bar2Length<0x40000000)) {found=pci;break;}
 }
 iterator->release();
 IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeSRIOVVFScanCount",scanned,32);
 IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeSRIOVVFRegistryPCI",registryPCI,32);
 IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeSRIOVVFMatchCount",matches,32);
 return found;
}
static IOPCIDevice *xelpFindSRIOVVF() { return xelpFindSRIOVVFCandidate(true); }
// Match-notification callbacks run while the registry is publishing.  Do not
// walk the registry from that context: mark the already-matched S80 service
// directly and let the normal scanner consume the candidate property later.
static bool xelpMarkSRIOVVFPublish(void *, void *, IOService *service, IONotifier *) {
 if(!(checkKernelArgument("-wex-sriov-vf") || checkKernelArgument("-wex-sriov-vf-observe")))return true;
 const auto name=service ? service->getName() : nullptr;
 const bool vfName=name && (strcmp(name,"S80")==0 || strstr(name,"S80@")!=nullptr);
 if(!vfName)return true;
 auto root=IORegistryEntry::getRegistryRoot();
 root->setProperty("WhateverXeVFServiceSeen",name);
 root->setProperty("WhateverXeVFNameSeen",true);
 root->setProperty("WhateverXeSRIOVVFMatchCount",1U,32);
 auto pci=static_cast<IOPCIDevice *>(service);
 if(xelpPublishedVF)xelpPublishedVF->release();
 xelpPublishedVF=pci;
 xelpPublishedVF->retain();
 pci->setProperty("WhateverXeSRIOVVFCandidate",true);
 pci->setProperty("WhateverXeSRIOVVFObserved",true);
 return true;
}
void XeHost::init() {
 callbackHost=this;
 if(checkKernelArgument("-wex-sriov-vf") || checkKernelArgument("-wex-sriov-vf-observe")) {
  if(auto matching=IOService::serviceMatching("IOPCIDevice")) {
   IOService::addMatchingNotification(gIOFirstPublishNotification,matching,xelpMarkSRIOVVFPublish,nullptr,nullptr,0);
   matching->release();
  }
 }
 PE_parse_boot_argn("gfxrst",&resetFramebuffer,sizeof(resetFramebuffer));
 if(resetFramebuffer>=FB_TOTAL)resetFramebuffer=FB_DETECT;
 lilu.onPatcherLoadForce([](void *u,KernelPatcher &p){static_cast<XeHost *>(u)->processKernel(p);},this);
 lilu.onKextLoadForce(nullptr,0,[](void *u,KernelPatcher &p,size_t i,mach_vm_address_t a,size_t s){static_cast<XeHost *>(u)->processKext(p,i,a,s);},this);
 if(resetFramebuffer!=FB_NONE)lilu.onKextLoadForce(&graphics);
 lilu.onKextLoadForce(&backlight);
 xe.init();
 XeDataAccess::init();
}
void XeHost::processKernel(KernelPatcher &patcher) {
 // A passed-through VF is a secondary PCI service when QEMU's std VGA remains
 // the primary display. Publish the observation before DeviceInfo admission so
 // the diagnostic path remains visible even if the host has no Intel primary.
 auto vfObservation=xelpFindSRIOVVFCandidate(false);
 if(vfObservation) {
  vfObservation->setProperty("WhateverXeSRIOVVFObserved",true);
  const auto bar2=vfObservation->getDeviceMemoryWithRegister(0x18);
  const uint64_t bar2Length=bar2 ? bar2->getLength() : 0;
  vfObservation->setProperty("WhateverXeSRIOVVFBar2Length",bar2Length,64);
 }
 auto info=DeviceInfo::create();if(!info)return;
 auto gpu=OSDynamicCast(IOPCIDevice,info->videoBuiltin);
 // Match WEG ordering: PCI config reads require a published IOPCIDevice.
 bool published=gpu && WIOKit::awaitPublishing(gpu);
 if(published) {
  if(auto vf=xelpFindSRIOVVF()) {
   vf->setProperty("WhateverXeSRIOVVFObserved",true);
   if(checkKernelArgument("-wex-sriov-vf")) {
    info->videoBuiltin=vf;gpu=vf;published=WIOKit::awaitPublishing(gpu);
   }
  }
 } else if(checkKernelArgument("-wex-sriov-vf")) {
  // QEMU std VGA may leave DeviceInfo without a native videoBuiltin.  The
  // published S80 VF is still a valid logical A780 target for the opt-in
  // relay path, so admit it directly instead of abandoning the host setup.
  if(auto vf=xelpFindSRIOVVF()) {
   vf->setProperty("WhateverXeSRIOVVFObserved",true);
   info->videoBuiltin=vf;gpu=vf;published=WIOKit::awaitPublishing(gpu);
  }
 }
 const auto profile=published ? XELP::deviceProfile(gpu) : XELP::Profile {};
 // The host must dispatch framebuffer patches and accelerator rejection
 // hooks even when the selected profile deliberately has no Render backend.
 active=profile.displayAdmitted();
 const auto display=XELP::displayAdapter(profile.family);
 backlightEnabled=active && (display==XELP::DisplayAdapter::TGL || display==XELP::DisplayAdapter::ADLS ||
  display==XELP::DisplayAdapter::ADLP || display==XELP::DisplayAdapter::ADLN) &&
  info->firmwareVendor!=DeviceInfo::FirmwareVendor::Apple;
 backlightGPU=backlightEnabled ? gpu : nullptr;
 if(published) {
  uint32_t selection[] {1,profile.identity,profile.revision,profile.effectiveIdentity,
   static_cast<uint32_t>(profile.family),static_cast<uint32_t>(profile.display),
   static_cast<uint32_t>(profile.gt),active};
  gpu->setProperty("WhateverXeDeviceSelection",selection,sizeof(selection));
  if(!gpu->getProperty("model"))if(const auto name=XELP::model(profile))gpu->setProperty("model",const_cast<char *>(name),strlen(name)+1);
 }
 // Capture the unmodified firmware/GT state before admitting a new backend.
 if(!active && (profile.family==XELP::Family::AlderLakeS ||
                profile.family==XELP::Family::AlderLakeP))xe.processKernel(patcher,info);
 if(active) {
  XeDataAccess::processKernel(patcher);
  // Xe admission reads physical PCI identity. Do not install WEG's ID spoof hooks.
  // WEG normalizes the ME device discovered by Lilu for Apple's name matching.
  if(auto mei=info->managementEngine) {
   const auto name=mei->getName();
   if(!name || strcmp(name,"IMEI"))WIOKit::renameDevice(mei,"IMEI");
  }
  if(info->firmwareVendor!=DeviceInfo::FirmwareVendor::Apple) {
   if(strcmp(gpu->getName(),"IGPU"))WIOKit::renameDevice(gpu,"IGPU");
   if(!gpu->getProperty("built-in")){uint8_t value=0;gpu->setProperty("built-in",&value,1);}
   if(resetFramebuffer==FB_DETECT && !info->reportedFramebufferIsConnectorLess)resetFramebuffer=FB_COPY;
  }
  gpu->setProperty("WhateverXePhysicalGGC",gpu->configRead16(0x50),32);
  xe.processKernel(patcher,info);
  if(resetFramebuffer==FB_COPY || resetFramebuffer==FB_ZEROFILL) {
   auto v=reinterpret_cast<vc_info *>(patcher.solveSymbol(KernelPatcher::KernelID,"_vinfo"));
   if(v){consoleVinfo=*v;gotConsoleVinfo=true;}else patcher.clearError();
  }
 }
 if(!active || resetFramebuffer==FB_DETECT){resetFramebuffer=FB_NONE;graphics.switchOff();}
 DeviceInfo::deleter(info);
}
void XeHost::processKext(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
 // PCI services can be published after processKernel.  Re-run the opt-in
 // observation during later kext notifications so a secondary VF is captured
 // once QEMU has exposed its S80 service, without changing normal admission.
 if(checkKernelArgument("-wex-sriov-vf") || checkKernelArgument("-wex-sriov-vf-observe")) {
  if(auto vf=xelpFindSRIOVVFCandidate(false)) {
   vf->setProperty("WhateverXeSRIOVVFObserved",true);
   const auto bar2=vf->getDeviceMemoryWithRegister(0x18);
   const uint64_t bar2Length=bar2 ? bar2->getLength() : 0;
   vf->setProperty("WhateverXeSRIOVVFBar2Length",bar2Length,64);
  }
 }
 if(!active)return;
 if(backlight.loadIndex==index) {
  if(backlightEnabled) {
   KernelPatcher::RouteRequest route("__ZN15AppleIntelPanel10setDisplayEP9IODisplay",wrapPanelSetDisplay,orgPanelSetDisplay);
   const bool ready=patcher.routeMultiple(index,&route,1,address,size);
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeBacklightProfileReady",ready);
   patcher.clearError();
  }
  return;
 }
 if(XeDataAccess::processKext(patcher,index,address,size))return;
 if(graphics.loadIndex==index) {
  gIOFBVerboseBootPtr=patcher.solveSymbol<uint8_t *>(index,"__ZL16gIOFBVerboseBoot",address,size);
  if(gIOFBVerboseBootPtr){KernelPatcher::RouteRequest route("__ZN13IOFramebuffer6initFBEv",wrapFramebufferInit,orgFramebufferInit);patcher.routeMultiple(index,&route,1,address,size);}
  patcher.clearError();return;
 }
 xe.processKext(patcher,index,address,size);
}
bool XeHost::wrapPanelSetDisplay(IOService *panel,IOService *display) {
 // Add only the missing UID-19 profile for an actual internal 16-bit PWM
 // display. Keep Apple's/custom profiles and the native selection algorithm.
 // No string patch, product-ID list, ACPI path or model-specific table.
 auto provider=panel ? panel->getProvider() : nullptr;
 auto uid=provider ? OSDynamicCast(OSString,provider->getProperty("_UID")) : nullptr;
 auto product=display ? OSDynamicCast(OSNumber,display->getProperty("DisplayProductID")) : nullptr;
 bool owned=false;
 IOFramebuffer *framebuffer=nullptr;
 for(auto entry=display;entry;entry=entry->getProvider()) {
  // IOGraphicsFamily is loaded later than this Lilu plugin. A direct
  // IOFramebuffer::metaClass import is not available in our dependency set.
  if(!framebuffer)framebuffer=static_cast<IOFramebuffer *>(entry->metaCast("IOFramebuffer"));
  if(entry==callbackHost->backlightGPU){owned=true;break;}
 }
 // setDisplay precedes publication of IODisplayParameters. Query the same
 // three-word (current,min,max) attribute as AppleIntelPanelA itself.
 uintptr_t pwm[3] {};
 const bool internal=display && display->metaCast("AppleBacklightDisplay");
 const bool range=owned && internal && framebuffer &&
  framebuffer->getAttributeForConnection(0,'bklt',pwm)==kIOReturnSuccess;
 OSDictionary *heldProfiles=nullptr;
 if(range && uid && product && pwm[1]<=UINT32_MAX && pwm[2]<=UINT32_MAX &&
    XeBacklightProfile::eligible(uid->isEqualTo("19") ? 19 : 0,pwm[1],pwm[2],internal)) {
  auto original=OSDynamicCast(OSDictionary,panel->getProperty("ApplePanels"));
  if(original && !panel->getProperty("ApplePanel")) {
   char key[32];snprintf(key,sizeof(key),"F19T%04x",product->unsigned32BitValue());
   if(!original->getObject(key) && !original->getObject("F19")) {
    auto copy=OSDictionary::withDictionary(original);
    auto curve=OSData::withBytes(XeBacklightProfile::curve,sizeof(XeBacklightProfile::curve));
    if(copy && curve && copy->setObject(key,curve) && panel->setProperty("ApplePanels",copy)) {
     // Native removes ApplePanels before retaining its selected OSData in
     // ApplePanel. Keep the candidate dictionary alive across that handoff.
     heldProfiles=copy;
     panel->setProperty("WhateverXeBacklightProfile",key);
    }
    if(curve)curve->release();
    if(copy && !heldProfiles)copy->release();
   }
  }
 }
 uint32_t inputs[]={1,owned,uid && uid->isEqualTo("19"),range ? static_cast<uint32_t>(pwm[1]) : ~0U,
  range ? static_cast<uint32_t>(pwm[2]) : 0,0};
 const bool result=FunctionCast(wrapPanelSetDisplay,callbackHost->orgPanelSetDisplay)(panel,display);
 if(heldProfiles)heldProfiles->release();
 if(panel) {
  inputs[5]=result;
  panel->setProperty("WhateverXeBacklightMatch",inputs,sizeof(inputs));
 }
 return result;
}
void XeHost::wrapFramebufferInit(IOFramebuffer *fb) {
	bool backCopy = callbackHost->gotConsoleVinfo && callbackHost->resetFramebuffer == FB_COPY;
	bool zeroFill  = callbackHost->gotConsoleVinfo && callbackHost->resetFramebuffer == FB_ZEROFILL;
	auto &info = callbackHost->consoleVinfo;

	// Copy back usually happens in a separate call to frameBufferInit
	// Furthermore, v_baseaddr may not be available on subsequent calls, so we have to copy
	if (backCopy && info.v_baseaddr) {
		// Note, this buffer is left allocated and never freed, yet there actually is no way to free it.
		callbackHost->consoleBuffer = Buffer::create<uint8_t>(info.v_rowbytes * info.v_height);
		if (callbackHost->consoleBuffer)
			lilu_os_memcpy(callbackHost->consoleBuffer, reinterpret_cast<uint8_t *>(info.v_baseaddr), info.v_rowbytes * info.v_height);
		else
			SYSLOG("wex", "console buffer allocation failure");
		// Even if we may succeed next time, it will be unreasonably dangerous
		info.v_baseaddr = 0;
	}

	uint8_t verboseBoot = *callbackHost->gIOFBVerboseBootPtr;
	// For back copy we need a console buffer and no verbose
	backCopy = backCopy && callbackHost->consoleBuffer && !verboseBoot;

	// Now check if the resolution and parameters match
	if (backCopy || zeroFill) {
		IODisplayModeID mode;
		IOIndex depth;
		IOPixelInformation pixelInfo;

		if (fb->getCurrentDisplayMode(&mode, &depth) == kIOReturnSuccess &&
			fb->getPixelInformation(mode, depth, kIOFBSystemAperture, &pixelInfo) == kIOReturnSuccess) {
			DBGLOG("wex", "fb info 1: %d:%d %u:%u:%u",
				   mode, depth, pixelInfo.bytesPerRow, pixelInfo.bytesPerPlane, pixelInfo.bitsPerPixel);
			DBGLOG("wex", "fb info 2: %u:%u %s %u:%u:%u",
				   pixelInfo.componentCount, pixelInfo.bitsPerComponent, pixelInfo.pixelFormat, pixelInfo.flags, pixelInfo.activeWidth, pixelInfo.activeHeight);

			if (info.v_rowbytes != pixelInfo.bytesPerRow || info.v_width != pixelInfo.activeWidth ||
				info.v_height != pixelInfo.activeHeight || info.v_depth != pixelInfo.bitsPerPixel) {
				backCopy = zeroFill = false;
				DBGLOG("wex", "this display has different mode");
			}
		} else {
			DBGLOG("wex", "failed to obtain display mode");
			backCopy = zeroFill = false;
		}
	}

	// For whatever reason not resetting Intel framebuffer (back copy mode) twice works better.
	if (!backCopy) *callbackHost->gIOFBVerboseBootPtr = 1;
	FunctionCast(wrapFramebufferInit, callbackHost->orgFramebufferInit)(fb);
	if (!backCopy) *callbackHost->gIOFBVerboseBootPtr = verboseBoot;

	// Finish the framebuffer initialisation by filling with black or copying the image back.
	if (FramebufferViewer::getVramMap(fb)) {
		auto src = reinterpret_cast<uint8_t *>(callbackHost->consoleBuffer);
		auto dst = reinterpret_cast<uint8_t *>(FramebufferViewer::getVramMap(fb)->getVirtualAddress());
		if (backCopy) {
			DBGLOG("wex", "attempting to copy...");
			// Here you can actually draw at your will, but looks like only on Intel.
			// On AMD you technically can draw too, but it happens for a very short while, and is not worth it.
			lilu_os_memcpy(dst, src, info.v_rowbytes * info.v_height);
		} else if (zeroFill) {
			// On AMD we do a zero-fill to ensure no visual glitches.
			DBGLOG("wex", "doing zero-fill...");
			memset(dst, 0, info.v_rowbytes * info.v_height);
		}
	}
}

