package android.media;

public final class AudioFormat {
    public static final int ENCODING_INVALID = 0;
    public static final int ENCODING_DEFAULT = 1;
    public static final int ENCODING_PCM_16BIT = 2;
    public static final int ENCODING_PCM_8BIT = 3;
    public static final int ENCODING_PCM_FLOAT = 4;
    public static final int CHANNEL_IN_MONO = 0x10;
    public static final int CHANNEL_IN_STEREO = 0x0c;
    public static final int CHANNEL_OUT_MONO = 0x4;
    public static final int CHANNEL_OUT_STEREO = 0xc;
    public static final int CHANNEL_CONFIGURATION_MONO = 2;
    public static final int CHANNEL_CONFIGURATION_STEREO = 3;

    private AudioFormat() {}
}
