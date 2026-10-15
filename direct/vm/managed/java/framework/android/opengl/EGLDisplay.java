package android.opengl;

public class EGLDisplay extends EGLObjectHandle {
    EGLDisplay(long handle) {
        super(handle);
    }

    public boolean equals(Object o) {
        return o instanceof EGLDisplay && (((EGLDisplay) o).getNativeHandle() == getNativeHandle());
    }
}
