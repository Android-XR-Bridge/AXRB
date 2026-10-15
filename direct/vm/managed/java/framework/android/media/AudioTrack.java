package android.media;

/** Output sink that consumes data at the nominal rate without playing it. */
public class AudioTrack {
    public static final int STATE_UNINITIALIZED = 0;
    public static final int STATE_INITIALIZED = 1;
    public static final int PLAYSTATE_STOPPED = 1;
    public static final int PLAYSTATE_PAUSED = 2;
    public static final int PLAYSTATE_PLAYING = 3;
    public static final int MODE_STATIC = 0;
    public static final int MODE_STREAM = 1;
    public static final int SUCCESS = 0;
    public static final int ERROR = -1;
    public static final int ERROR_BAD_VALUE = -2;
    public static final int WRITE_BLOCKING = 0;
    public static final int WRITE_NON_BLOCKING = 1;

    private int mState = STATE_INITIALIZED;
    private int mPlayState = PLAYSTATE_STOPPED;

    public AudioTrack(int streamType, int sampleRateInHz, int channelConfig, int audioFormat, int bufferSizeInBytes,
            int mode) {}

    public AudioTrack(int streamType, int sampleRateInHz, int channelConfig, int audioFormat, int bufferSizeInBytes,
            int mode, int sessionId) {}

    public static int getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat) {
        return 4096;
    }

    public static int getNativeOutputSampleRate(int streamType) {
        return 48000;
    }

    public int getState() {
        return mState;
    }

    public int getPlayState() {
        return mPlayState;
    }

    public void play() {
        mPlayState = PLAYSTATE_PLAYING;
    }

    public void pause() {
        mPlayState = PLAYSTATE_PAUSED;
    }

    public void stop() {
        mPlayState = PLAYSTATE_STOPPED;
    }

    public void flush() {}

    public void release() {
        mState = STATE_UNINITIALIZED;
        mPlayState = PLAYSTATE_STOPPED;
    }

    public int write(byte[] audioData, int offsetInBytes, int sizeInBytes) {
        return sizeInBytes;
    }

    public int write(short[] audioData, int offsetInShorts, int sizeInShorts) {
        return sizeInShorts;
    }

    public int write(java.nio.ByteBuffer audioData, int sizeInBytes, int writeMode) {
        audioData.position(audioData.position() + sizeInBytes);
        return sizeInBytes;
    }

    public int setStereoVolume(float leftGain, float rightGain) {
        return SUCCESS;
    }

    public int setVolume(float gain) {
        return SUCCESS;
    }

    public int getAudioSessionId() {
        return 1;
    }
}
