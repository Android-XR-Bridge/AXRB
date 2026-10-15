package android.opengl;

public class EGLSurface extends EGLObjectHandle {
    EGLSurface(long handle) {
        super(handle);
    }

    public boolean equals(Object o) {
        return o instanceof EGLSurface && (((EGLSurface) o).getNativeHandle() == getNativeHandle());
    }
}
