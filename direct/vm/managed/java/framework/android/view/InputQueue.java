package android.view;

/** No queue is ever created: input reaches VR games through OpenXR. */
public final class InputQueue {
    public interface Callback {
        void onInputQueueCreated(InputQueue queue);

        void onInputQueueDestroyed(InputQueue queue);
    }

    InputQueue() {}
}
