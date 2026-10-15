package android.opengl;

public class EGLContext extends EGLObjectHandle {
    EGLContext(long handle) {
        super(handle);
    }

    public boolean equals(Object o) {
        return o instanceof EGLContext && (((EGLContext) o).getNativeHandle() == getNativeHandle());
    }
}
