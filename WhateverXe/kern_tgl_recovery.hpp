// Exact TGL donor layout, with single-pass reset and bounded BCS completion.
// The caller must qualify the binary and select host scheduler 5 first.
#ifndef kern_tgl_recovery_hpp
#define kern_tgl_recovery_hpp
#include <stdint.h>

namespace TGLRecovery {
template<typename T> inline T &field(void *p, unsigned offset) {
	return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+offset);
}

using SubmitStamp=void (*)(void *);
using WaitStamp=bool (*)(void *,uint32_t *,uint32_t,uint32_t);
struct StampOps { SubmitStamp submit; WaitStamp wait; };
enum class StampStatus : uint8_t { NotRun, Complete, TimedOut };

struct Result {
	uintptr_t ring, context, affected;
	uint32_t index, failuresBefore, failuresAfter;
	bool resetOK;
	uint8_t engine;
	StampStatus stamp;
	uint32_t targetStamp, observedStamp;
	uint32_t attempts;
	uintptr_t activeBefore, activeAfter;
};

// Keep the ICL single-pass contract for a failed reset and non-BCS engines.
// On successful Blit reset, replay the donor's bounded completion sequence:
// resume, increment the affected event stamp, submit and wait once. A timeout
// accounts a failure and returns; never retry under IOAccel's recovery locks.
inline Result resetAndResume(void *channel,const StampOps &stamp) {
	void *ring=field<void *>(channel,0x130);
	void *context=field<void *>(ring,0x20);
	void *affected=field<void *>(context,0xB8); // ICL uses B0, NOT compatible.
	Result result {reinterpret_cast<uintptr_t>(ring),reinterpret_cast<uintptr_t>(context),
		reinterpret_cast<uintptr_t>(affected),affected ? field<uint32_t>(affected,0x20) : 0,
		affected ? field<uint32_t>(affected,0x17C) : 0,0,false,0,StampStatus::NotRun,0,0,0,0,0};
	using Reset=bool (*)(void *,void *);
	// TGL resetGraphicsEngine (43576) first tries the engine's pending soft
	// steps (engine+0xA4 bits 3/2). When the engine does not acknowledge one
	// it clears that bit and returns success WITHOUT resetting, so the hung
	// batch is never dropped and IOAccel restarts the channel forever. Native
	// callers loop until a stamp lands; repeat only while a soft step was
	// pending, so the full GDRST path and processGPUReset (resetRing plus the
	// completing stamp) run in this pass.
	auto engineFlags=[&]() -> uint32_t {
		void *engine=field<void *>(ring,0x8B8+8*field<uint8_t>(ring,0x40));
		return engine ? field<uint32_t>(engine,0xA4)&0xC : 0;
	};
	// IGHardwareCommandStreamer5::processGPUReset only drops the hung batch
	// (resetRing + resubmitStampToRing) through the active port element.
	// IGScheduler5::processGPUReset(type) -> scheduler+0xCA0[type].
	auto active=[&]() -> uintptr_t {
		void *accelerator=field<void *>(ring,0x10);
		void *scheduler=accelerator ? field<void *>(accelerator,0x1250) : nullptr;
		void *streamer=scheduler ? field<void *>(scheduler,0xCA0+8*field<uint8_t>(ring,0x40)) : nullptr;
		if (!streamer) return 1;
		const uint32_t port=field<uint32_t>(streamer,0x50);
		return (field<uintptr_t>(streamer,0x30+16*(port&1))&~uintptr_t(0xF)) |
			(field<uintptr_t>(streamer,0x30+16*(1-(port&1))) ? 4 : 0) | (port&3);
	};
	result.activeBefore=active();
	for (unsigned attempt=0;attempt<3;++attempt) {
		const bool soft=engineFlags()!=0;
		result.resetOK=field<Reset>(field<void *>(ring,0),0x168)(ring,context);
		result.attempts=attempt+1;
		if (!soft || !result.resetOK) break;
	}
	result.activeAfter=active();
	result.engine=field<uint8_t>(ring,0x40); // Donor's Blit ring type is 2.
	if (affected) {
		if (!result.resetOK) ++field<uint32_t>(affected,0x17C);
		result.failuresAfter=field<uint32_t>(affected,0x17C);
	}
	// Reload after reset, as the native ICL caller does.
	void *owner=field<void *>(channel,0x18);
	void *scheduler=field<void *>(owner,0x1250); // ICL uses1280.
	using Resume=void (*)(void *);
	field<Resume>(field<void *>(scheduler,0),0x180)(scheduler);
	if (result.resetOK && result.engine==2 && affected) {
		// Native resume can change owner state; reload exactly as the TGL caller.
		owner=field<void *>(channel,0x18);
		void *events=field<void *>(owner,0x380);
		using IncrementStamp=void (*)(void *,int);
		field<IncrementStamp>(field<void *>(events,0),0x1D8)(events,field<int>(affected,0x20));
		stamp.submit(affected);
		result.targetStamp=field<uint32_t>(affected,0x178);
		// Donor sleepForStamp returns true for timeout, false if stamp reached.
		const bool timedOut=stamp.wait(ring,&result.observedStamp,result.targetStamp,1000000);
		result.stamp=timedOut ? StampStatus::TimedOut : StampStatus::Complete;
		if (timedOut) ++field<uint32_t>(affected,0x17C);
		result.failuresAfter=field<uint32_t>(affected,0x17C);
	}
	return result;
}
}
#endif
