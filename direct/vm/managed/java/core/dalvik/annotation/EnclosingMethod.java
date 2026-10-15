package dalvik.annotation;

import java.lang.annotation.Annotation;

public interface EnclosingMethod extends Annotation {
    Object value();
}
