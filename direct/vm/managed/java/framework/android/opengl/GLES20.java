package android.opengl;

/** No OpenGL ES implementation exists (see libGLES.c): every call is a no-op. */
public class GLES20 {
    public static void glActiveTexture(int a0) {}

    public static void glAttachShader(int a0, int a1) {}

    public static void glBindBuffer(int a0, int a1) {}

    public static void glBindFramebuffer(int a0, int a1) {}

    public static void glBindTexture(int a0, int a1) {}

    public static void glBufferData(int a0, int a1, java.nio.Buffer a2, int a3) {}

    public static int glCheckFramebufferStatus(int a0) {
        return 0;
    }

    public static void glClear(int a0) {}

    public static void glClearColor(float a0, float a1, float a2, float a3) {}

    public static void glColorMask(boolean a0, boolean a1, boolean a2, boolean a3) {}

    public static void glCompileShader(int a0) {}

    public static int glCreateProgram() {
        return 0;
    }

    public static int glCreateShader(int a0) {
        return 0;
    }

    public static void glDeleteBuffers(int a0, int[] a1, int a2) {}

    public static void glDeleteFramebuffers(int a0, int[] a1, int a2) {}

    public static void glDeleteProgram(int a0) {}

    public static void glDeleteShader(int a0) {}

    public static void glDeleteTextures(int a0, int[] a1, int a2) {}

    public static void glDetachShader(int a0, int a1) {}

    public static void glDisable(int a0) {}

    public static void glDisableVertexAttribArray(int a0) {}

    public static void glDrawArrays(int a0, int a1, int a2) {}

    public static void glEnable(int a0) {}

    public static void glEnableVertexAttribArray(int a0) {}

    public static void glFlush() {}

    public static void glFramebufferTexture2D(int a0, int a1, int a2, int a3, int a4) {}

    public static void glGenBuffers(int a0, int[] a1, int a2) {}

    public static void glGenFramebuffers(int a0, int[] a1, int a2) {}

    public static void glGenTextures(int a0, int[] a1, int a2) {}

    public static int glGetAttribLocation(int a0, java.lang.String a1) {
        return 0;
    }

    public static int glGetError() {
        return 0;
    }

    public static void glGetIntegerv(int a0, int[] a1, int a2) {}

    public static java.lang.String glGetProgramInfoLog(int a0) {
        return null;
    }

    public static void glGetProgramiv(int a0, int a1, int[] a2, int a3) {}

    public static java.lang.String glGetShaderInfoLog(int a0) {
        return null;
    }

    public static void glGetShaderiv(int a0, int a1, int[] a2, int a3) {}

    public static java.lang.String glGetString(int a0) {
        return null;
    }

    public static void glGetTexParameteriv(int a0, int a1, int[] a2, int a3) {}

    public static int glGetUniformLocation(int a0, java.lang.String a1) {
        return 0;
    }

    public static boolean glIsEnabled(int a0) {
        return false;
    }

    public static void glLinkProgram(int a0) {}

    public static void glReadPixels(int a0, int a1, int a2, int a3, int a4, int a5, java.nio.Buffer a6) {}

    public static void glShaderSource(int a0, java.lang.String a1) {}

    public static void glTexImage2D(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, java.nio.Buffer a8) {}

    public static void glTexParameteri(int a0, int a1, int a2) {}

    public static void glUniform1i(int a0, int a1) {}

    public static void glUseProgram(int a0) {}

    public static void glVertexAttribPointer(int a0, int a1, int a2, boolean a3, int a4, int a5) {}

    public static void glViewport(int a0, int a1, int a2, int a3) {}
}
