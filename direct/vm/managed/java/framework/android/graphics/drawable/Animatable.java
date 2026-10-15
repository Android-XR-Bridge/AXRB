package android.graphics.drawable;

public interface Animatable {
    void start();

    void stop();

    boolean isRunning();
}