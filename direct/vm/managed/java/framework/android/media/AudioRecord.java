package android.media;

import java.nio.ByteBuffer;

/** No microphone: initialisation fails, as on a device without one. */
public class AudioRecord {
    public static final int STATE_UNINITIALIZED = 0;
    public static final int STATE_INITIALIZED = 1;
    public static final int RECORDSTATE_STOPPED = 1;
    public static final int RECORDSTATE_RECORDING = 3;
    public static final int SUCCESS = 0;
    public static final int ERROR = -1;
    public static final int ERROR_BAD_VALUE = -2;
    public static final int ERROR_INVALID_OPERATION = -3;

    public AudioRecord(int audioSource, int sampleRateInHz, int channelConfig, int audioFormat,
            int bufferSizeInBytes) {}

    public static int getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat) {
        return ERROR_BAD_VALUE;
    }

    public int getState() {
        return STATE_UNINITIALIZED;
    }

    public int getRecordingState() {
        return RECORDSTATE_STOPPED;
    }

    public void startRecording() {
        throw new IllegalStateException("startRecording() called on an uninitialized AudioRecord.");
    }

    public void stop() {}

    public void release() {}

    public int read(byte[] audioData, int offsetInBytes, int sizeInBytes) {
        return ERROR_INVALID_OPERATION;
    }

    public int read(short[] audioData, int offsetInShorts, int sizeInShorts) {
        return ERROR_INVALID_OPERATION;
    }

    public int read(ByteBuffer audioBuffer, int sizeInBytes) {
        return ERROR_INVALID_OPERATION;
    }
}
