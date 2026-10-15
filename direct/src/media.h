#pragma once

#include <cstdint>

namespace arun::vm {
class Runtime;
}

namespace axrb {

// MediaPlayer (Media Foundation video decoding) and the frame exchange behind
// android.view.Surface.
void register_media_natives(arun::vm::Runtime* rt);
void art_media_init();
// AMediaExtractor and AMediaCodec on Media Foundation (media_codec.cpp).
void art_media_codec_init();
// Makes a top-down RGBA frame the newest frame of the Surface with this media
// handle, as MediaPlayer playback does.
void media_publish_frame(int64_t surface, uint32_t width, uint32_t height, const uint8_t* rgba);

}  // namespace axrb
