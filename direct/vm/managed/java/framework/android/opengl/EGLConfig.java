package android.opengl;

public class EGLConfig extends EGLObjectHandle {
    EGLConfig(long handle) {
        super(handle);
    }

    public boolean equals(Object o) {
        return o instanceof EGLConfig && (((EGLConfig) o).getNativeHandle() == getNativeHandle());
    }
}
