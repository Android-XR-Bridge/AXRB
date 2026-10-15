package java.lang;

import java.io.Serializable;
import java.util.Objects;

public final class StackTraceElement implements Serializable {
    private final String declaringClass;
    private final String methodName;
    private final String fileName;
    private final int lineNumber;

    public StackTraceElement(String declaringClass, String methodName, String fileName, int lineNumber) {
        this.declaringClass = Objects.requireNonNull(declaringClass, "Declaring class is null");
        this.methodName = Objects.requireNonNull(methodName, "Method name is null");
        this.fileName = fileName;
        this.lineNumber = lineNumber;
    }

    public String getClassName() {
        return declaringClass;
    }

    public String getMethodName() {
        return methodName;
    }

    public String getFileName() {
        return fileName;
    }

    public int getLineNumber() {
        return lineNumber;
    }

    public boolean isNativeMethod() {
        return lineNumber == -2;
    }

    @Override
    public String toString() {
        String where;
        if (isNativeMethod()) {
            where = "(Native Method)";
        } else if (fileName == null) {
            where = "(Unknown Source)";
        } else if (lineNumber >= 0) {
            where = "(" + fileName + ":" + lineNumber + ")";
        } else {
            where = "(" + fileName + ")";
        }
        return declaringClass + "." + methodName + where;
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof StackTraceElement)) return false;
        StackTraceElement e = (StackTraceElement) obj;
        return declaringClass.equals(e.declaringClass) && methodName.equals(e.methodName)
                && Objects.equals(fileName, e.fileName) && lineNumber == e.lineNumber;
    }

    @Override
    public int hashCode() {
        return 31 * (31 * declaringClass.hashCode() + methodName.hashCode()) + Objects.hashCode(fileName) + lineNumber;
    }
}
