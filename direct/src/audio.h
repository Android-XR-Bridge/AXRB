#pragma once

namespace axrb {

// Registers the host functions behind the guest's libaaudio.so (WASAPI output).
void audio_init();
bool audio_active();

}  // namespace axrb
