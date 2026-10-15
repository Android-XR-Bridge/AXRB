#pragma once
#include "berberis/guest_state/guest_state.h"
namespace berberis {
extern "C" void berberis_entry_HandleLiteCounterThresholdReached();
void InvalidateGuestRange(GuestAddr start, GuestAddr end);
}
