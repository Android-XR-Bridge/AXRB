package com.axrb.mediaprobe;

import android.app.Activity;
import android.content.res.AssetManager;
import android.os.Bundle;

/** Decodes an MP4 asset with AMediaExtractor and AMediaCodec (media_codec_probe.c). */
public class MediaCodecProbe extends Activity {
    static native String run(AssetManager assets);

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        System.loadLibrary("mediaprobe");
        String result = run(getAssets());
        System.out.println(result);
        System.exit(result.endsWith("MEDIA_CODEC_PROBE_PASS") ? 0 : 1);
    }
}
